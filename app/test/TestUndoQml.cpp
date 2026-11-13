/** @file TestUndoQml.cpp
 * @brief 实际 QML 删除与选择器失效路径回归测试
 */
#include "ComponentData.h"
#include "FeatureContext.h"
#include "FeatureHandler.h"
#include "FeatureRegistrar.h"
#include "FeatureSystem.h"
#include "GeometryBuilder.h"
#include "GeometryData.h"
#include "MeshData.h"
#include "ModelLayer.h"
#include "QFeatureInfo.h"
#include "QModelManager.h"
#include "QRenderWindow.h"
#include "QSelection.h"
#include "TreeModel.h"
#include "TreeModelTestHelpers.h"
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickVTKItem.h>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <utility>

Q_IMPORT_QML_PLUGIN(app_corePlugin)
Q_IMPORT_QML_PLUGIN(app_modelPlugin)
Q_IMPORT_QML_PLUGIN(app_renderPlugin)
Q_IMPORT_QML_PLUGIN(app_model_systemsPlugin)
Q_IMPORT_QML_PLUGIN(app_model_systems_algoPlugin)
Q_IMPORT_QML_PLUGIN(app_model_systems_ioPlugin)
Q_IMPORT_QML_PLUGIN(app_model_systems_editPlugin)
Q_IMPORT_QML_PLUGIN(app_model_systems_featurePlugin)

namespace {
class ManySelectors : public systems::feature::FeatureHandler {
public:
    void setup(systems::feature::FeatureRegistrar& reg, systems::feature::FeatureContext&) override
    {
        for (int i = 0; i < 50; ++i)
            reg.addParameter({ ArgTypeEnum::Selector, "目标", "Face", "" });
    }
    std::any execute(systems::feature::FeatureContext& ctx) override
    {
        return ctx.model.addModel("undo fixture", { });
    }
};
QGuiApplication& application()
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    static int argc = 1;
    static char name[] = "TestUndoQml";
    static char* argv[] = { name, nullptr };
    static QGuiApplication app(argc, argv);
    return app;
}
std::unique_ptr<QObject> load(QQmlEngine& engine, const char* file, const QVariantMap& properties = { })
{
    const QUrl url(QString("qrc:/undo-ui/") + file);
    QQmlComponent component(&engine);
    if (QString(file) == "ObjectTree.qml") {
        QFile source(":" + url.path());
        REQUIRE(source.open(QIODevice::ReadOnly));
        component.setData("import UndoUi 1.0\n" + source.readAll(), url);
    } else {
        component.loadUrl(url);
    }
    INFO(component.errorString().toStdString());
    REQUIRE(component.isReady());
    std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
    INFO(component.errorString().toStdString());
    REQUIRE(object);
    return object;
}
struct QmlFixture {
    QTemporaryDir directory;
    std::string executable;
    QQmlEngine engine;
    std::unique_ptr<QObject> session;
    std::unique_ptr<QObject> sidebar;
    std::unique_ptr<QObject> tree;
    std::unique_ptr<QObject> viewport;
    QModelManager* manager;
    QObject* app;
    QObject* selection_state;

    QmlFixture()
    {
        static const int tree_type = qmlRegisterType<TreeModel>("UndoUi", 1, 0, "TreeModel");
        static const int theme_type = qmlRegisterSingletonType(QUrl("qrc:/undo-ui/Theme.qml"), "UndoUi", 1, 0, "Theme");
        REQUIRE(directory.isValid());
        executable = (directory.path() + "/isolated/Test.exe").toStdString();
        QModelManager::argv0 = executable;
        engine.rootContext()->setContextProperty("Theme", engine.singletonInstance<QObject*>(theme_type));
        session = load(engine, "OperationSession.qml");
        sidebar = load(engine, "SideBar.qml", { { "session", QVariant::fromValue(session.get()) } });
        tree = load(engine, "ObjectTree.qml");
        viewport = load(engine, "CentralRenderArea.qml");
        manager = engine.singletonInstance<QModelManager*>("app.model", "QModelManager");
        app = engine.singletonInstance<QObject*>("app.core", "App");
        REQUIRE(manager);
        REQUIRE(app);
        selection_state = app->property("selection").value<QObject*>();
        REQUIRE(selection_state);
    }
    ~QmlFixture()
    {
        app->setProperty("activeOperation", QVariant::fromValue(QJSValue(QJSValue::NullValue)));
        QModelManager::argv0 = { };
    }
};

// 结构事件即时同步；这里只泵一次事件循环，让实际 QML 链路完成收尾。
void pumpEvents()
{
    QCoreApplication::processEvents();
}

}

TEST_CASE("Actual QML synchronizes reversed mesh and geometry visibility across tree refresh", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    component->geometry = std::make_unique<GeometryData>();
    components.push_back(std::move(component));
    const Index model_id = model.addModel("visibility fixture", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds()[0];
    auto* tree = f.tree->findChild<TreeModel*>();
    auto* viewport = f.viewport->findChild<QQuickVTKItem*>();
    REQUIRE(tree);
    REQUIRE(viewport);
    REQUIRE(tree->refresh());

    for (auto [mesh_visible, geometry_visible] : { std::make_pair(true, false),
             std::make_pair(false, false), std::make_pair(false, true), std::make_pair(true, true) }) {
        // 触发实际视口信号，覆盖 CentralRenderArea 到 ObjectTree 的回传链路。
        REQUIRE(QMetaObject::invokeMethod(viewport, "componentVisibilityChanged",
            Q_ARG(Index, component_id), Q_ARG(bool, mesh_visible), Q_ARG(bool, geometry_visible)));
        for (int round = 0; round < 2; ++round) {
            const auto index = tree->findIndexByNodeId(component_id, 1);
            REQUIRE(index.isValid());
            CHECK(tree->data(index, TreeModel::IsVisibleRole).toBool() == (mesh_visible || geometry_visible));
            CHECK(tree->data(tree->parent(index), TreeModel::IsVisibleRole).toBool() == (mesh_visible || geometry_visible));
            REQUIRE(tree->rowCount(index) == 2);
            for (int i = 0; i < tree->rowCount(index); ++i) {
                const auto child = tree->index(i, 0, index);
                const bool is_mesh = tree->data(child, TreeModel::NodeTypeRole).toInt() == static_cast<int>(TreeNode::NodeType::Mesh);
                CHECK(tree->data(child, TreeModel::IsVisibleRole).toBool() == (is_mesh ? mesh_visible : geometry_visible));
            }
            // 树重建后仍需保留各子节点状态，不能退回统一的组件开关。
            REQUIRE(tree->refresh());
        }
    }
}

TEST_CASE("Actual QML keeps reported visibility when a subtype re-enters the tree", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    component->geometry = std::make_unique<GeometryData>();
    components.push_back(std::move(component));
    const Index model_id = model.addModel("visibility restore", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds()[0];
    auto* tree = f.tree->findChild<TreeModel*>();
    auto* viewport = f.viewport->findChild<QQuickVTKItem*>();
    REQUIRE(tree);
    REQUIRE(viewport);
    REQUIRE(tree->refresh());

    const auto report = [&](bool mesh_visible, bool geometry_visible) {
        REQUIRE(QMetaObject::invokeMethod(viewport, "componentVisibilityChanged",
            Q_ARG(Index, component_id), Q_ARG(bool, mesh_visible), Q_ARG(bool, geometry_visible)));
    };
    const auto subtype_visible = [&](TreeNode::NodeType type) {
        const QModelIndex component_index = tree->findIndexByNodeId(component_id, 1);
        REQUIRE(component_index.isValid());
        for (int i = 0; i < tree->rowCount(component_index); ++i) {
            const QModelIndex child = tree->index(i, 0, component_index);
            if (tree->data(child, TreeModel::NodeTypeRole).toInt() == static_cast<int>(type))
                return tree->data(child, TreeModel::IsVisibleRole).toBool();
        }
        return false;
    };

    // 隐藏几何：完整二元结果一次应用。
    report(true, false);
    CHECK(subtype_visible(TreeNode::NodeType::Mesh));
    CHECK_FALSE(subtype_visible(TreeNode::NodeType::Geometry));

    // 删除网格并等待结构同步完成，树中只剩隐藏的几何。
    auto component_operator = model.getComponentOperator(component_id);
    REQUIRE(component_operator);
    component_operator->removeMesh();
    model.flushNotifications();
    REQUIRE(waitForCondition([&] {
        const QModelIndex idx = tree->findIndexByNodeId(component_id, 1);
        return idx.isValid() && !tree->data(idx, TreeModel::IsVisibleRole).toBool();
    }));
    CHECK_FALSE(tree->data(tree->findIndexByNodeId(component_id, 1), TreeModel::IsVisibleRole).toBool());

    // 撤销恢复：结构同步尚未执行时回传"网格可见、几何隐藏"，创建后不得被默认值覆盖。
    component_operator->replaceMesh(std::make_unique<MeshData>());
    model.flushNotifications();
    report(true, false);
    REQUIRE(waitForCondition([&] {
        const QModelIndex idx = tree->findIndexByNodeId(component_id, 1);
        if (!idx.isValid())
            return false;
        for (int i = 0; i < tree->rowCount(idx); ++i) {
            const QModelIndex child = tree->index(i, 0, idx);
            if (tree->data(child, TreeModel::NodeTypeRole).toInt() == static_cast<int>(TreeNode::NodeType::Mesh))
                return tree->data(child, TreeModel::IsVisibleRole).toBool();
        }
        return false;
    }));

    CHECK(subtype_visible(TreeNode::NodeType::Mesh));
    CHECK_FALSE(subtype_visible(TreeNode::NodeType::Geometry));
    CHECK(tree->data(tree->findIndexByNodeId(component_id, 1), TreeModel::IsVisibleRole).toBool());
}

TEST_CASE("Actual QML applies hidden state reported before a component enters the tree", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    ComponentDatas components;
    components.push_back(std::make_unique<ComponentData>());
    const Index model_id = model.addModel("pending component", std::move(components));
    auto* tree = f.tree->findChild<TreeModel*>();
    REQUIRE(tree);
    REQUIRE(tree->refresh());

    // 组件已存在于模型与视口，但合并刷新尚未把它加入对象树。
    auto model_operator = model.getModelOperator(model_id);
    REQUIRE(model_operator);
    auto created = std::make_unique<ComponentData>();
    created->geometry = std::make_unique<GeometryData>();
    const Index component_id = model_operator->addGeometryComponent(std::move(created));
    REQUIRE_FALSE(tree->findIndexByNodeId(component_id, 1).isValid());

    REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "hideAllNodes"));
    REQUIRE(waitForCondition([&] {
        const QModelIndex idx = tree->findIndexByNodeId(component_id, 1);
        return idx.isValid() && !tree->data(idx, TreeModel::IsVisibleRole).toBool();
    }));

    const QModelIndex component_index = tree->findIndexByNodeId(component_id, 1);
    REQUIRE(component_index.isValid());
    CHECK_FALSE(tree->data(component_index, TreeModel::IsVisibleRole).toBool());
    REQUIRE(tree->rowCount(component_index) == 1);
    CHECK_FALSE(tree->data(tree->index(0, 0, component_index), TreeModel::IsVisibleRole).toBool());
}

TEST_CASE("Actual QML refreshes component tri-state when one subtype changes", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    component->geometry = std::make_unique<GeometryData>();
    component->geometry->setRootShape(GeometryBuilder::makeBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0));
    components.push_back(std::move(component));
    const Index model_id = model.addModel("tri-state fixture", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds().front();

    auto* tree = f.tree->findChild<TreeModel*>();
    auto* viewport = f.viewport->findChild<QRenderWindow*>();
    REQUIRE(tree);
    REQUIRE(viewport);
    REQUIRE(tree->refresh());
    viewport->onComponentChanged(component_id);

    const QModelIndex component_index = tree->findIndexByNodeId(component_id, 1);
    REQUIRE(component_index.isValid());
    int visibility_updates = 0;
    QObject::connect(tree, &QAbstractItemModel::dataChanged, tree,
        [&](const QModelIndex& first, const QModelIndex&, const QList<int>& roles) {
            if (first == component_index && roles.contains(TreeModel::VisibilityStateRole))
                ++visibility_updates;
        });

    // 只隐藏几何：组件布尔显隐仍为可见，但三态必须变为部分显示并通知组件行。
    viewport->setGeometryVisibility(component_id, false);
    CHECK(tree->data(component_index, TreeModel::IsVisibleRole).toBool());
    CHECK(tree->data(component_index, TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    CHECK(visibility_updates >= 1);
}

TEST_CASE("Actual QML keeps all show/hide state consistent across repeated toggles", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);

    ComponentDatas components;
    auto mesh_geometry = std::make_unique<ComponentData>();
    mesh_geometry->mesh = std::make_unique<MeshData>();
    mesh_geometry->geometry = std::make_unique<GeometryData>();
    mesh_geometry->geometry->setRootShape(GeometryBuilder::makeBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0));
    components.push_back(std::move(mesh_geometry));

    auto geometry_only = std::make_unique<ComponentData>();
    geometry_only->geometry = std::make_unique<GeometryData>();
    geometry_only->geometry->setRootShape(GeometryBuilder::makeBox(2.0, 0.0, 0.0, 1.0, 1.0, 1.0));
    components.push_back(std::move(geometry_only));

    auto mesh_only = std::make_unique<ComponentData>();
    mesh_only->mesh = std::make_unique<MeshData>();
    components.push_back(std::move(mesh_only));

    const Index model_id = model.addModel("toggle fixture", std::move(components));
    const auto component_ids = model.modelById(model_id)->componentIds();
    REQUIRE(component_ids.size() == 3);

    auto* tree = f.tree->findChild<TreeModel*>();
    auto* viewport = f.viewport->findChild<QRenderWindow*>();
    REQUIRE(tree);
    REQUIRE(viewport);
    REQUIRE(tree->refresh());
    for (Index component_id : component_ids)
        viewport->onComponentChanged(component_id);

    std::map<Index, std::pair<bool, bool>> last;
    QObject::connect(viewport, &QRenderWindow::componentVisibilityChanged, viewport,
        [&last](Index component_id, bool mesh_visible, bool geometry_visible) {
            last[component_id] = { mesh_visible, geometry_visible };
        });

    const auto check_state = [&](bool visible) {
        const QModelIndex model_index = tree->findIndexByNodeId(model_id, 0);
        REQUIRE(model_index.isValid());
        CHECK(tree->data(model_index, TreeModel::IsVisibleRole).toBool() == visible);
        CHECK(tree->data(model_index, TreeModel::VisibilityStateRole).toInt()
            == (visible ? Qt::Checked : Qt::Unchecked));
        for (Index component_id : component_ids) {
            const QModelIndex component_index = tree->findIndexByNodeId(component_id, 1);
            REQUIRE(component_index.isValid());
            CHECK(tree->data(component_index, TreeModel::IsVisibleRole).toBool() == visible);
            CHECK(tree->data(component_index, TreeModel::VisibilityStateRole).toInt()
                == (visible ? Qt::Checked : Qt::Unchecked));
            const ComponentData* component = model.findComponent(component_id);
            REQUIRE(component);
            REQUIRE(last.contains(component_id));
            CHECK(last.at(component_id)
                == std::make_pair(static_cast<bool>(component->mesh) && visible,
                                  static_cast<bool>(component->geometry) && visible));
        }
    };

    for (int cycle = 0; cycle < 4; ++cycle) {
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "hideAllNodes"));
        pumpEvents();
        check_state(false);
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "showAllNodes"));
        pumpEvents();
        check_state(true);
    }
}

//! @brief QML 只验证真实入口的目标范围；隐藏面恢复由视口及数据管线测试覆盖。
TEST_CASE("Actual QML visibility entries dispatch to the intended components", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    const auto add_model = [&](int component_count) {
        ComponentDatas components;
        for (int i = 0; i < component_count; ++i) {
            auto component = std::make_unique<ComponentData>();
            component->mesh = std::make_unique<MeshData>();
            components.push_back(std::move(component));
        }
        return model.addModel("visibility fixture", std::move(components));
    };
    const Index model_id = add_model(2);
    const Index other_model_id = add_model(1);
    const auto component_ids = model.modelById(model_id)->componentIds();
    const Index id = component_ids[0];
    const Index second_id = component_ids[1];
    const Index other_id = model.modelById(other_model_id)->componentIds()[0];
    auto* tree = f.tree->findChild<TreeModel*>();
    auto* viewport = f.viewport->findChild<QRenderWindow*>();
    REQUIRE(tree);
    REQUIRE(viewport);
    REQUIRE(tree->refresh());
    // 只建立 GUI 显隐状态；视口不挂入窗口，因此不会创建 VTK/OpenGL 场景。
    for (Index component_id : { id, second_id, other_id }) {
        auto data = f.manager->getModelQuery()->getMeshDataByComponent(component_id);
        REQUIRE(data);
        viewport->loadMeshData(component_id, *data);
        viewport->setMeshVisibility(component_id, false);
    }
    REQUIRE(viewport->window() == nullptr);
    std::map<Index, std::pair<bool, bool>> notifications;
    QObject::connect(viewport, &QRenderWindow::componentVisibilityChanged, viewport,
                     [&notifications](Index component_id, bool mesh_visible, bool geometry_visible) {
                         notifications[component_id] = { mesh_visible, geometry_visible };
                     });
    QString show_entry;
    bool show_scene = false;
    bool show_model = false;
    SECTION("object tree toolbar")
    {
        show_scene = true;
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "showAllNodes"));
    }
    SECTION("viewport context menu")
    {
        show_scene = true;
        auto* menu = f.viewport->findChild<QObject*>("viewportVisibilityMenu");
        REQUIRE(menu);
        QObject* show_all = nullptr;
        for (auto* item : menu->findChildren<QObject*>()) {
            if (item->property("shown").isValid() && item->property("text").toString() == "全部显示")
                show_all = item;
        }
        REQUIRE(show_all);
        REQUIRE(QMetaObject::invokeMethod(show_all, "triggered"));
    }
    SECTION("object tree component show") { show_entry = QString("showNode-1-%1").arg(id); }
    SECTION("object tree mesh show") { show_entry = QString("showNode-2-%1").arg(id); }
    SECTION("object tree model show")
    {
        show_model = true;
        show_entry = QString("showNode-0-%1").arg(model_id);
    }
    if (!show_entry.isEmpty()) {
        // 软件场景图仅布局对象树，保留真实菜单的创建、可见性和触发接线检查。
        QQuickWindow host;
        host.resize(300, 600);
        auto* tree_page = qobject_cast<QQuickItem*>(f.tree.get());
        REQUIRE(tree_page);
        tree_page->setParentItem(host.contentItem());
        tree_page->setSize(QSizeF(300, 600));
        const auto cleanup = qScopeGuard([&] {
            host.hide();
            tree_page->setParentItem(nullptr);
        });
        host.show();
        QCoreApplication::processEvents();
        auto* tree_view = f.tree->findChild<QObject*>("objectTreeView");
        REQUIRE(tree_view);
        REQUIRE(QMetaObject::invokeMethod(tree_view, "expandRecursively", Q_ARG(int, -1), Q_ARG(int, -1)));
        REQUIRE(QMetaObject::invokeMethod(tree_view, "forceLayout"));
        auto node_index = tree->findIndexByNodeId(id, 1);
        if (show_model)
            node_index = node_index.parent();
        else if (show_entry.startsWith("showNode-2-"))
            node_index = tree->index(0, 0, node_index);
        QQuickItem* delegate = nullptr;
        REQUIRE(QMetaObject::invokeMethod(tree_view, "itemAtIndex",
            Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(QModelIndex, node_index)));
        REQUIRE(delegate);
        auto* show = delegate->findChild<QObject*>(show_entry);
        REQUIRE(show);
        CHECK(show->property("text").toString() == "显示");
        auto* menu = show->property("menu").value<QObject*>();
        REQUIRE(menu);
        REQUIRE(QMetaObject::invokeMethod(menu, "open"));
        QCoreApplication::processEvents();
        CHECK(show->property("visible").toBool());
        CHECK(show->property("height").toDouble() > 0);
        REQUIRE(QMetaObject::invokeMethod(show, "triggered"));
        REQUIRE(QMetaObject::invokeMethod(menu, "close"));
    }
    const auto check_visibility = [&](Index component_id, bool expected) {
        const auto index = tree->findIndexByNodeId(component_id, 1);
        CHECK(tree->data(index, TreeModel::IsVisibleRole).toBool() == expected);
        CHECK(notifications.contains(component_id) == expected);
        if (expected)
            CHECK(notifications.at(component_id) == std::make_pair(true, false));
    };
    check_visibility(id, true);
    check_visibility(second_id, show_scene || show_model);
    check_visibility(other_id, show_scene);
    REQUIRE(viewport->window() == nullptr);
    if (show_scene) {
        notifications.clear();
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "reverseDisplayed"));
        REQUIRE(notifications.size() == 3);
        for (const auto& [component_id, visibility] : notifications) {
            CHECK(visibility == std::make_pair(false, false));
            CHECK_FALSE(tree->data(tree->findIndexByNodeId(component_id, 1), TreeModel::IsVisibleRole).toBool());
        }
    }
}

TEST_CASE("Actual QML aligns face and component visibility menu entries", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto* menu = f.viewport->findChild<QObject*>("viewportVisibilityMenu");
    REQUIRE(menu);
    std::map<QString, QObject*> items;
    for (auto* item : menu->findChildren<QObject*>()) {
        if (item->property("shown").isValid()) {
            const auto text = item->property("text").toString();
            REQUIRE(items.count(text) == 0);
            items[text] = item;
        }
    }
    REQUIRE(items.size() == 6);
    for (const auto& mode : { QString("Face"), QString("Component") }) {
        f.selection_state->setProperty("selectMode", mode);
        for (bool has_picks : { false, true }) {
            // 传入真实 QSelection，覆盖 C++ 选择快照到 QML 菜单的判断路径。
            auto data = std::make_unique<Selection>();
            data->type = mode == "Face" ? ElementEnum::Face : ElementEnum::Component;
            data->component_id = 7;
            if (has_picks)
                data->ids = { 0, 1 };
            QSelection selection(std::move(data));
            REQUIRE(QMetaObject::invokeMethod(menu, "refreshSelection",
                Q_ARG(QVariant, QVariant::fromValue(&selection))));
            CHECK(menu->property("hasPicks").toBool() == has_picks);
            for (const auto& text : { QString("隐藏"), QString("显示"), QString("隔离") })
                CHECK(items.at(text)->property("shown").toBool() == has_picks);
            for (const auto& text : { QString("全部隐藏"), QString("全部显示") })
                CHECK(items.at(text)->property("shown").toBool() == !has_picks);
            CHECK(items.at("反转显示")->property("shown").toBool());
        }
    }
    // 尚未取得选择快照时也应恢复全局操作，不能保留上次菜单的选择状态。
    f.selection_state->setProperty("selectMode", "Face");
    REQUIRE(QMetaObject::invokeMethod(menu, "refreshSelection",
        Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(nullptr)))));
    CHECK_FALSE(menu->property("hasPicks").toBool());
    for (const auto& text : { QString("隐藏"), QString("显示"), QString("隔离") })
        CHECK_FALSE(items.at(text)->property("shown").toBool());
}

TEST_CASE("Actual QML follows successful deletion notifications and preserves selection on refusal", "[QML][undo]")
{
    application();
    QmlFixture f;
    auto* manager = f.manager;
    auto* selection_state = f.selection_state;
    auto& tree = f.tree;
    auto& model = *manager->getModelManager();
    auto* tree_model = tree->findChild<TreeModel*>();
    REQUIRE(tree_model);
    model.setUndoRecorder(nullptr);
    ComponentDatas components;
    components.push_back(std::make_unique<ComponentData>());
    components.push_back(std::make_unique<ComponentData>());
    components.push_back(std::make_unique<ComponentData>());
    const Index mid = model.addModel("fixture", std::move(components));
    const Index cid = model.modelById(mid)->componentIds()[0];
    const Index remaining_cid = model.modelById(mid)->componentIds()[1];
    const Index other_cid = model.modelById(mid)->componentIds()[2];
    selection_state->setProperty("activeModelId", mid);
    selection_state->setProperty("activeComponentId", cid);
    auto operation = model.beginWriteOperation(false);
    for (int depth = 0; depth < 2; ++depth) {
        REQUIRE(QMetaObject::invokeMethod(tree.get(), "deleteNode", Q_ARG(QVariant, depth == 0 ? mid : cid),
            Q_ARG(QVariant, depth), Q_ARG(QVariant, 0), Q_ARG(QVariant, cid)));
        CHECK(selection_state->property("activeModelId").toInt() == mid);
        CHECK(selection_state->property("activeComponentId").toInt() == cid);
        CHECK(model.findComponent(cid));
    }
    operation.reset();
    // 删除非当前项不改变选择。
    model.removeComponent(other_cid);
    CHECK(selection_state->property("activeModelId").toInt() == mid);
    CHECK(selection_state->property("activeComponentId").toInt() == cid);
    const Index other_mid = model.addModel("other", { });
    model.removeModel(other_mid);
    CHECK(selection_state->property("activeModelId").toInt() == mid);
    CHECK(selection_state->property("activeComponentId").toInt() == cid);
    // 直接修改模型层，验证界面响应实际删除通知，独立于对象树命令入口。
    model.removeComponent(cid);
    // 删除当前选中组件后重置选中，不自动选中剩余组件。
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    pumpEvents();
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    // 后续删除不改变已清空的选中。
    model.removeComponent(remaining_cid);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    pumpEvents();
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    model.removeModel(mid);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    pumpEvents();
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    // 整体删除带组件的当前模型同样清空选择。
    ComponentDatas model_components;
    model_components.push_back(std::make_unique<ComponentData>());
    model_components.push_back(std::make_unique<ComponentData>());
    const Index selected_mid = model.addModel("selected", std::move(model_components));
    selection_state->setProperty("activeModelId", selected_mid);
    selection_state->setProperty("activeComponentId", model.modelById(selected_mid)->componentIds()[0]);
    model.removeModel(selected_mid);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    pumpEvents();
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
}

TEST_CASE("Actual QML toggles object tree selection across all rows", "[QML][tree][selection]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    auto* tree_view = f.tree->findChild<QObject*>("objectTreeView");
    auto* selection_state = f.selection_state;
    REQUIRE(tree_model);
    REQUIRE(tree_view);
    REQUIRE(selection_state);

    auto make_component = [] {
        auto component = std::make_unique<ComponentData>();
        component->mesh = std::make_unique<MeshData>();
        component->mesh->vertex_positions_ = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };
        component->mesh->vertex_count_ = 3;
        // 生成 Mesh -> 3D 子节点，用于覆盖 depth>2 行。
        component->mesh->solid_types_.assign(1, 0);
        return component;
    };
    ComponentDatas components;
    components.push_back(make_component());
    components.push_back(make_component());
    const Index model_id = model.addModel("selection fixture", std::move(components));
    const auto component_ids = model.modelById(model_id)->componentIds();
    REQUIRE(component_ids.size() == 2);
    REQUIRE(tree_model->refresh());

    auto* selection_model = tree_view->property("selectionModel").value<QItemSelectionModel*>();
    REQUIRE(selection_model);

    const auto apply = [&](Index node_id, int depth, Index component_id) {
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "applyNodeSelection",
            Q_ARG(QVariant, node_id), Q_ARG(QVariant, depth), Q_ARG(QVariant, component_id)));
    };
    const auto check_selected = [&](Index node_id, int depth) {
        const QModelIndex index = tree_model->findIndexByNodeId(node_id, depth);
        REQUIRE(index.isValid());
        CHECK(selection_model->isSelected(index));
        CHECK(selection_model->selectedIndexes().size() == 1);
    };
    const auto check_empty = [&] {
        CHECK_FALSE(selection_model->hasSelection());
        CHECK(selection_state->property("activeModelId").toInt() == -1);
        CHECK(selection_state->property("activeComponentId").toInt() == -1);
    };

    // Component 左键选中，再次点击取消。
    apply(component_ids[0], 1, component_ids[0]);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_ids[0]);
    check_selected(component_ids[0], 1);
    apply(component_ids[0], 1, component_ids[0]);
    check_empty();

    // Model 选中时关联其第一个 Component，再次点击取消。
    apply(model_id, 0, -1);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_ids[0]);
    check_selected(model_id, 0);
    apply(model_id, 0, -1);
    check_empty();

    // 不同组件之间切换选中。
    apply(component_ids[0], 1, component_ids[0]);
    apply(component_ids[1], 1, component_ids[1]);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_ids[1]);
    check_selected(component_ids[1], 1);

    // depth>2 行同样可选中，再次点击取消。
    const QModelIndex component_index = tree_model->findIndexByNodeId(component_ids[1], 1);
    REQUIRE(component_index.isValid());
    const QModelIndex mesh_index = tree_model->index(0, 0, component_index);
    REQUIRE(mesh_index.isValid());
    const QModelIndex sub_index = tree_model->index(0, 0, mesh_index);
    REQUIRE(sub_index.isValid());
    const Index sub_node_id = tree_model->data(sub_index, TreeModel::NodeIdRole).toInt();
    CHECK(tree_model->data(sub_index, TreeModel::NodeTypeRole).toInt()
        == static_cast<int>(TreeNode::NodeType::D3));
    apply(sub_node_id, 3, component_ids[1]);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_ids[1]);
    check_selected(sub_node_id, 3);
    apply(sub_node_id, 3, component_ids[1]);
    check_empty();

    // undo/redo 应用后清空对象树选中态。
    auto* undo = f.manager->getUndoStackAdaptor();
    REQUIRE(undo);
    REQUIRE(QMetaObject::invokeMethod(undo, "applied"));
    check_empty();

    model.removeModel(model_id);
    pumpEvents();
    check_empty();
}

TEST_CASE("Actual QML handles real object tree clicks and Delete key", "[QML][tree][selection]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    auto* tree_view_object = f.tree->findChild<QObject*>("objectTreeView");
    auto* selection_state = f.selection_state;
    REQUIRE(tree_model);
    REQUIRE(tree_view_object);
    REQUIRE(selection_state);

    ComponentDatas components;
    components.push_back(std::make_unique<ComponentData>());
    const Index model_id = model.addModel("click fixture", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds().front();
    REQUIRE(tree_model->refresh());

    QQuickWindow host;
    host.resize(300, 600);
    auto* tree_page = qobject_cast<QQuickItem*>(f.tree.get());
    REQUIRE(tree_page);
    tree_page->setParentItem(host.contentItem());
    tree_page->setSize(QSizeF(300, 600));
    const auto cleanup = qScopeGuard([&] {
        host.hide();
        tree_page->setParentItem(nullptr);
    });
    host.show();
    QCoreApplication::processEvents();
    REQUIRE(QMetaObject::invokeMethod(tree_view_object, "expandRecursively",
        Q_ARG(int, -1), Q_ARG(int, -1)));
    REQUIRE(QMetaObject::invokeMethod(tree_view_object, "forceLayout"));
    QCoreApplication::processEvents();

    const QModelIndex component_index = tree_model->findIndexByNodeId(component_id, 1);
    REQUIRE(component_index.isValid());
    QQuickItem* delegate = nullptr;
    REQUIRE(QMetaObject::invokeMethod(tree_view_object, "itemAtIndex",
        Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(QModelIndex, component_index)));
    REQUIRE(delegate);
    auto* tree_view = qobject_cast<QQuickItem*>(tree_view_object);
    REQUIRE(tree_view);
    const QPointF pos = host.contentItem()->mapFromItem(delegate,
        delegate->width() / 2, delegate->height() / 2);
    const auto click = [&](Qt::MouseButton button) {
        QMouseEvent press(QEvent::MouseButtonPress, pos, host.mapToGlobal(pos.toPoint()),
            button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, host.mapToGlobal(pos.toPoint()),
            button, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &release);
        QCoreApplication::processEvents();
    };

    click(Qt::LeftButton);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_id);

    click(Qt::LeftButton);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);

    click(Qt::LeftButton);
    CHECK(selection_state->property("activeComponentId").toInt() == component_id);
    tree_view->forceActiveFocus();
    QKeyEvent key_press(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(&host, &key_press);
    QKeyEvent key_release(QEvent::KeyRelease, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(&host, &key_release);
    QCoreApplication::processEvents();
    CHECK(model.findComponent(component_id) == nullptr);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
}

TEST_CASE("Actual QML routes deep row right-click to unavailable menu", "[QML][tree][selection]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    auto* tree_view_object = f.tree->findChild<QObject*>("objectTreeView");
    auto* selection_state = f.selection_state;
    REQUIRE(tree_model);
    REQUIRE(tree_view_object);
    REQUIRE(selection_state);

    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    component->mesh->solid_types_.assign(1, 0);
    components.push_back(std::move(component));
    const Index model_id = model.addModel("deep menu fixture", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds().front();
    REQUIRE(tree_model->refresh());

    QQuickWindow host;
    host.resize(300, 600);
    auto* tree_page = qobject_cast<QQuickItem*>(f.tree.get());
    REQUIRE(tree_page);
    tree_page->setParentItem(host.contentItem());
    tree_page->setSize(QSizeF(300, 600));
    const auto cleanup = qScopeGuard([&] {
        host.hide();
        tree_page->setParentItem(nullptr);
    });
    host.show();
    QCoreApplication::processEvents();
    REQUIRE(QMetaObject::invokeMethod(tree_view_object, "expandRecursively",
        Q_ARG(int, -1), Q_ARG(int, -1)));
    REQUIRE(QMetaObject::invokeMethod(tree_view_object, "forceLayout"));
    QCoreApplication::processEvents();

    const QModelIndex component_index = tree_model->findIndexByNodeId(component_id, 1);
    REQUIRE(component_index.isValid());
    const QModelIndex mesh_index = tree_model->index(0, 0, component_index);
    REQUIRE(mesh_index.isValid());
    const QModelIndex sub_index = tree_model->index(0, 0, mesh_index);
    REQUIRE(sub_index.isValid());
    QQuickItem* delegate = nullptr;
    REQUIRE(QMetaObject::invokeMethod(tree_view_object, "itemAtIndex",
        Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(QModelIndex, sub_index)));
    REQUIRE(delegate);
    const QPointF pos = host.contentItem()->mapFromItem(delegate,
        delegate->width() / 2, delegate->height() / 2);
    const auto click = [&](Qt::MouseButton button) {
        QMouseEvent press(QEvent::MouseButtonPress, pos, host.mapToGlobal(pos.toPoint()),
            button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, host.mapToGlobal(pos.toPoint()),
            button, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &release);
        QCoreApplication::processEvents();
    };

    click(Qt::LeftButton);
    click(Qt::RightButton);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_id);
    auto* item = f.tree->findChild<QObject*>("nodeMenuUnavailableItem");
    REQUIRE(item);
    auto* menu = item->property("menu").value<QObject*>();
    REQUIRE(menu);
    CHECK(menu->property("visible").toBool());
    REQUIRE(QMetaObject::invokeMethod(menu, "close"));
}

TEST_CASE("Actual QML emphasizes only model and component rows", "[QML][tree][selection]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    REQUIRE(tree_model);

    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    components.push_back(std::move(component));
    const Index model_id = model.addModel("emphasis fixture", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds().front();
    REQUIRE(tree_model->refresh());

    const QModelIndex component_index = tree_model->findIndexByNodeId(component_id, 1);
    REQUIRE(component_index.isValid());
    const QModelIndex mesh_index = tree_model->index(0, 0, component_index);
    REQUIRE(mesh_index.isValid());
    const Index mesh_node_id = tree_model->data(mesh_index, TreeModel::NodeIdRole).toInt();

    const auto select = [&](Index node_id, int depth, Index owner_id) {
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "applyNodeSelection",
            Q_ARG(QVariant, node_id), Q_ARG(QVariant, depth), Q_ARG(QVariant, owner_id)));
    };
    const auto emphasized = [&](Index node_id, int depth) {
        QVariant result;
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "isNodeEmphasized",
            Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, node_id), Q_ARG(QVariant, depth)));
        return result.toBool();
    };

    // 选中 Component：Model 与 Component 加粗，depth=2 行不加粗。
    select(component_id, 1, component_id);
    CHECK(emphasized(model_id, 0));
    CHECK(emphasized(component_id, 1));
    CHECK_FALSE(emphasized(mesh_node_id, 2));

    // 选中 depth=2 行：父级继续加粗，该行只高亮不加粗。
    select(mesh_node_id, 2, component_id);
    CHECK(emphasized(model_id, 0));
    CHECK(emphasized(component_id, 1));
    CHECK_FALSE(emphasized(mesh_node_id, 2));

    // 再次点击取消后全部恢复常规字重。
    select(mesh_node_id, 2, component_id);
    CHECK_FALSE(emphasized(model_id, 0));
    CHECK_FALSE(emphasized(component_id, 1));
    CHECK_FALSE(emphasized(mesh_node_id, 2));

    model.removeModel(model_id);
    pumpEvents();
}

TEST_CASE("Actual QML provides an unavailable menu for deep object tree rows", "[QML][tree][selection]")
{
    application();
    QmlFixture f;

    auto* item = f.tree->findChild<QObject*>("nodeMenuUnavailableItem");
    REQUIRE(item);
    CHECK(item->property("text").toString() == "功能开发中");
    CHECK_FALSE(item->property("enabled").toBool());
}

TEST_CASE("Actual QML deletes the selected object tree node", "[QML][tree][selection][delete]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    auto* selection_state = f.selection_state;
    REQUIRE(tree_model);
    REQUIRE(selection_state);

    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    components.push_back(std::move(component));
    const Index model_id = model.addModel("delete fixture", std::move(components));
    const Index component_id = model.modelById(model_id)->componentIds().front();
    REQUIRE(tree_model->refresh());

    const auto select = [&](Index node_id, int depth, Index component_id) {
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "applyNodeSelection",
            Q_ARG(QVariant, node_id), Q_ARG(QVariant, depth), Q_ARG(QVariant, component_id)));
    };
    const auto delete_selected = [&] {
        QVariant deleted;
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "deleteSelectedNode",
            Q_RETURN_ARG(QVariant, deleted)));
        return deleted.toBool();
    };

    // 无选中时不执行删除。
    CHECK_FALSE(delete_selected());

    // 删除选中的 Mesh 子节点：组件保留，选中态保留。
    const QModelIndex component_index = tree_model->findIndexByNodeId(component_id, 1);
    REQUIRE(component_index.isValid());
    const QModelIndex mesh_index = tree_model->index(0, 0, component_index);
    REQUIRE(mesh_index.isValid());
    const Index mesh_node_id = tree_model->data(mesh_index, TreeModel::NodeIdRole).toInt();
    select(mesh_node_id, 2, component_id);
    CHECK(delete_selected());
    CHECK(model.findComponent(component_id));
    CHECK(model.findComponent(component_id)->mesh == nullptr);
    CHECK(selection_state->property("activeModelId").toInt() == model_id);
    CHECK(selection_state->property("activeComponentId").toInt() == component_id);
    // 等待树结构移除 Mesh 行后，行选择随之失效，重复 Delete 不会删除父组件。
    REQUIRE(waitForCondition([&] {
        return !tree_model->findIndexByNodeId(mesh_node_id, 2).isValid();
    }));
    CHECK_FALSE(delete_selected());
    CHECK(model.findComponent(component_id));

    // 删除选中的 Component：所属模型保留，选中态重置。
    select(component_id, 1, component_id);
    CHECK(delete_selected());
    CHECK(model.findComponent(component_id) == nullptr);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);

    // 删除选中的 Model：模型移除，选中态重置。
    select(model_id, 0, -1);
    CHECK(delete_selected());
    CHECK(model.modelById(model_id) == nullptr);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);

    pumpEvents();
}

TEST_CASE("Actual QML preserves expanded rows across incremental imports and deletes", "[QML][tree]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    auto* tree_view = f.tree->findChild<QObject*>("objectTreeView");
    REQUIRE(tree_model);
    REQUIRE(tree_view);

    auto make_components = [] {
        ComponentDatas components;
        auto component = std::make_unique<ComponentData>();
        component->mesh = std::make_unique<MeshData>();
        components.push_back(std::move(component));
        return components;
    };

    const Index first_model = model.addModel("first", make_components());
    const Index second_model = model.addModel("second", make_components());
    REQUIRE(tree_model->refresh());
    REQUIRE(QMetaObject::invokeMethod(tree_view, "expandRecursively",
        Q_ARG(int, -1), Q_ARG(int, -1)));
    REQUIRE(QMetaObject::invokeMethod(tree_view, "forceLayout"));
    QCoreApplication::processEvents();

    const Index target_component = model.modelById(second_model)->componentIds().front();
    auto row_of = [&](const QModelIndex& index) {
        int row = -1;
        REQUIRE(QMetaObject::invokeMethod(tree_view, "rowAtIndex",
            Q_RETURN_ARG(int, row), Q_ARG(QModelIndex, index)));
        return row;
    };
    auto is_expanded = [&](int row) {
        bool expanded = false;
        REQUIRE(QMetaObject::invokeMethod(tree_view, "isExpanded",
            Q_RETURN_ARG(bool, expanded), Q_ARG(int, row)));
        return expanded;
    };

    int resets = 0;
    QObject::connect(tree_model, &QAbstractItemModel::modelReset, [&] { ++resets; });

    const int initial_row = row_of(tree_model->findIndexByNodeId(target_component, 1));
    REQUIRE(initial_row >= 0);
    REQUIRE(is_expanded(initial_row));

    const Index third_model = model.addModel("third", make_components());
    QCoreApplication::processEvents();
    CHECK(resets == 0);
    int row = row_of(tree_model->findIndexByNodeId(target_component, 1));
    REQUIRE(row >= 0);
    CHECK(is_expanded(row));

    model.removeModel(first_model);
    QCoreApplication::processEvents();
    CHECK(resets == 0);
    row = row_of(tree_model->findIndexByNodeId(target_component, 1));
    REQUIRE(row >= 0);
    CHECK(is_expanded(row));

    model.removeModel(third_model);
    QCoreApplication::processEvents();
    CHECK(resets == 0);
    row = row_of(tree_model->findIndexByNodeId(target_component, 1));
    REQUIRE(row >= 0);
    CHECK(is_expanded(row));
}

TEST_CASE("Actual QML expands and collapses object tree subtrees", "[QML][tree]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto* tree_model = f.tree->findChild<TreeModel*>();
    auto* tree_view = f.tree->findChild<QObject*>("objectTreeView");
    REQUIRE(tree_model);
    REQUIRE(tree_view);

    auto make_component = [](bool with_solid, bool with_geometry) {
        auto component = std::make_unique<ComponentData>();
        component->mesh = std::make_unique<MeshData>();
        component->mesh->vertex_positions_ = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };
        component->mesh->vertex_count_ = 3;
        component->mesh->face_vertices_offset_.assign(2, 0);
        if (with_solid)
            component->mesh->solid_types_.assign(1, 0);
        if (with_geometry)
            component->geometry = std::make_unique<GeometryData>();
        return component;
    };

    ComponentDatas components_a;
    components_a.push_back(make_component(true, true));
    const Index model_a = model.addModel("levels-a", std::move(components_a));
    ComponentDatas components_b;
    components_b.push_back(make_component(false, false));
    const Index model_b = model.addModel("levels-b", std::move(components_b));
    REQUIRE(tree_model->refresh());

    const Index component_a = model.modelById(model_a)->componentIds().front();
    const Index component_b = model.modelById(model_b)->componentIds().front();
    const QModelIndex model_a_index = tree_model->findIndexByNodeId(model_a, 0);
    const QModelIndex model_b_index = tree_model->findIndexByNodeId(model_b, 0);
    const QModelIndex component_a_index = tree_model->findIndexByNodeId(component_a, 1);
    const QModelIndex component_b_index = tree_model->findIndexByNodeId(component_b, 1);
    REQUIRE(model_a_index.isValid());
    REQUIRE(model_b_index.isValid());
    REQUIRE(component_a_index.isValid());
    REQUIRE(component_b_index.isValid());
    const QModelIndex mesh_a_index = tree_model->index(0, 0, component_a_index);
    const QModelIndex mesh_b_index = tree_model->index(0, 0, component_b_index);
    REQUIRE(mesh_a_index.isValid());
    REQUIRE(mesh_b_index.isValid());
    const QModelIndex mesh_child_index = tree_model->index(0, 0, mesh_a_index);
    REQUIRE(mesh_child_index.isValid());

    auto settle = [&] {
        REQUIRE(QMetaObject::invokeMethod(tree_view, "forceLayout"));
        QCoreApplication::processEvents();
    };
    auto row_of = [&](const QModelIndex& index) {
        int row = -1;
        REQUIRE(QMetaObject::invokeMethod(tree_view, "rowAtIndex",
            Q_RETURN_ARG(int, row), Q_ARG(QModelIndex, index)));
        return row;
    };
    auto is_expanded = [&](int row) {
        bool expanded = false;
        REQUIRE(QMetaObject::invokeMethod(tree_view, "isExpanded",
            Q_RETURN_ARG(bool, expanded), Q_ARG(int, row)));
        return expanded;
    };
    auto rows = [&] { return tree_view->property("rows").toInt(); };
    auto expand_subtree = [&](const QModelIndex& index) {
        const int row = row_of(index);
        REQUIRE(row >= 0);
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "expandSubtree", Q_ARG(QVariant, row)));
        settle();
    };
    auto collapse_subtree = [&](const QModelIndex& index) {
        const int row = row_of(index);
        REQUIRE(row >= 0);
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "collapseSubtree", Q_ARG(QVariant, row)));
        settle();
    };
    auto expand_all = [&] {
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "expandSubtree", Q_ARG(QVariant, -1)));
        settle();
    };
    auto collapse_all = [&] {
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "collapseSubtree", Q_ARG(QVariant, -1)));
        settle();
    };

    REQUIRE(QMetaObject::invokeMethod(tree_view, "collapseRecursively", Q_ARG(int, -1)));
    settle();
    CHECK(rows() == 2);

    // 全部展开模型子树：递归到所有叶子，但不影响其他模型。
    expand_subtree(model_a_index);
    REQUIRE(row_of(model_a_index) >= 0);
    CHECK(is_expanded(row_of(model_a_index)));
    REQUIRE(row_of(component_a_index) >= 0);
    CHECK(is_expanded(row_of(component_a_index)));
    REQUIRE(row_of(mesh_a_index) >= 0);
    CHECK(is_expanded(row_of(mesh_a_index)));
    REQUIRE(row_of(mesh_child_index) >= 0);
    CHECK_FALSE(is_expanded(row_of(mesh_child_index)));
    REQUIRE(row_of(model_b_index) >= 0);
    CHECK_FALSE(is_expanded(row_of(model_b_index)));
    CHECK(rows() == 7);

    // 折叠组件子树只收起该组件及其后代。
    collapse_subtree(component_a_index);
    REQUIRE(row_of(component_a_index) >= 0);
    CHECK_FALSE(is_expanded(row_of(component_a_index)));
    CHECK(row_of(mesh_a_index) == -1);
    CHECK(rows() == 3);

    expand_subtree(component_a_index);
    REQUIRE(row_of(mesh_a_index) >= 0);
    CHECK(is_expanded(row_of(mesh_a_index)));
    CHECK(rows() == 7);

    collapse_subtree(mesh_a_index);
    REQUIRE(row_of(mesh_a_index) >= 0);
    CHECK_FALSE(is_expanded(row_of(mesh_a_index)));
    CHECK(row_of(mesh_child_index) == -1);
    CHECK(rows() == 5);

    expand_subtree(mesh_a_index);
    REQUIRE(row_of(mesh_child_index) >= 0);
    CHECK(rows() == 7);

    collapse_subtree(model_a_index);
    CHECK(row_of(component_a_index) == -1);
    CHECK(rows() == 2);

    expand_subtree(model_b_index);
    REQUIRE(row_of(model_b_index) >= 0);
    CHECK(is_expanded(row_of(model_b_index)));
    REQUIRE(row_of(component_b_index) >= 0);
    CHECK(is_expanded(row_of(component_b_index)));
    REQUIRE(row_of(mesh_b_index) >= 0);
    CHECK(is_expanded(row_of(mesh_b_index)));
    CHECK(rows() == 5);

    collapse_subtree(model_b_index);
    CHECK(rows() == 2);

    // 空白区全局展开/折叠作用于所有模型。
    expand_all();
    CHECK(rows() == 10);
    CHECK(is_expanded(row_of(model_a_index)));
    CHECK(is_expanded(row_of(model_b_index)));

    collapse_all();
    CHECK(rows() == 2);

    model.removeModel(model_a);
    model.removeModel(model_b);
    REQUIRE(waitForCondition([&] { return tree_model->rowCount() == 0; }));
    settle();
    CHECK(rows() == 0);
}

TEST_CASE("Actual undo clears offscreen selectors only after successful restoration", "[QML][undo]")
{
    application();
    QmlFixture f;
    auto* manager = f.manager;
    auto* app = f.app;
    auto* selection_state = f.selection_state;
    auto& model = *manager->getModelManager();
    auto& system = *manager->getFeatureSystemAdaptor()->featureSystem();
    systems::feature::HandlerMetaData meta;
    meta.name = "ManySelectors";
    REQUIRE(system.registerHandler(meta, systems::feature::FeatureSystem::SystemHandlerPtr { std::make_unique<ManySelectors>().release() }));
    auto* info = manager->getFeatureSystemAdaptor()->getFeaturesInfo().front();
    QJSValue active = f.engine.newObject();
    // 普通功能也使用统一入口身份；视口外参数仍由同一会话模型清理。
    active.setProperty("entryId", QStringLiteral("feature:ManySelectors:功能"));
    active.setProperty("isFeature", true);
    active.setProperty("info", f.engine.newQObject(info));
    app->setProperty("activeOperation", QVariant::fromValue(active));
    QSelection selection(std::make_unique<Selection>());
    selection.get()->ids = { 123 };
    selection.get()->type = ElementEnum::Face;
    for (int i = 0; i < 50; ++i)
        REQUIRE(manager->getFeatureSystemAdaptor()->setParameter("ManySelectors", i, QVariant::fromValue(&selection)));
    selection_state->setProperty("listeningSelectorIndex", 49);
    const auto revision = selection_state->property("selectionRevision").toInt();
    auto* undo = manager->getUndoStackAdaptor();
    int applied = 0;
    QObject::connect(undo, &QUndoStackAdaptor::applied, [&] { ++applied; });
    // 空历史和忙时拒绝都不得触发选择清理。
    undo->undo();
    CHECK(applied == 0);
    const Index mid = std::any_cast<Index>(system.invoke("ManySelectors"));
    REQUIRE(model.modelById(mid));
    REQUIRE(undo->canUndo());
    auto operation = model.beginWriteOperation(false);
    undo->undo();
    CHECK(applied == 0);
    CHECK(model.modelById(mid));
    CHECK(selection_state->property("listeningSelectorIndex").toInt() == 49);
    CHECK(selection_state->property("selectionRevision").toInt() == revision);
    for (int i = 0; i < 50; ++i)
        CHECK(*system.params("ManySelectors")->value(i).get<ArgTypeEnum::Selector>());
    operation.reset();
    undo->undo();
    CHECK(applied == 1);
    CHECK_FALSE(model.modelById(mid));
    CHECK(selection_state->property("listeningSelectorIndex").toInt() == -1);
    CHECK(selection_state->property("selectionRevision").toInt() > revision);
    for (int i = 0; i < 50; ++i)
        CHECK_FALSE(*system.params("ManySelectors")->value(i).get<ArgTypeEnum::Selector>());
    // redo 同样经真实恢复路径触发清理。
    REQUIRE(undo->canRedo());
    REQUIRE(manager->getFeatureSystemAdaptor()->setParameter("ManySelectors", 49, QVariant::fromValue(&selection)));
    selection_state->setProperty("listeningSelectorIndex", 49);
    undo->redo();
    CHECK(applied == 2);
    CHECK(model.modelById(mid));
    CHECK(selection_state->property("listeningSelectorIndex").toInt() == -1);
    CHECK_FALSE(*system.params("ManySelectors")->value(49).get<ArgTypeEnum::Selector>());
}
