/** @file TestUndoQml.cpp
 * @brief 实际 QML 删除与选择器失效路径回归测试
 */
#include "ComponentData.h"
#include "FeatureContext.h"
#include "FeatureHandler.h"
#include "FeatureRegistrar.h"
#include "FeatureSystem.h"
#include "GeometryData.h"
#include "MeshData.h"
#include "ModelLayer.h"
#include "QFeatureInfo.h"
#include "QModelManager.h"
#include "QRenderWindow.h"
#include "QSelection.h"
#include "TreeModel.h"
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickVTKItem.h>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
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

// 等实际模型刷新完成；定时器只限制失败等待，不依赖 UI 刷新周期。
bool waitForRefresh(TreeModel& tree_model)
{
    QEventLoop loop;
    bool refreshed = false;
    QObject::connect(&tree_model, &QAbstractItemModel::modelReset, &loop, [&] {
        refreshed = true;
        loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(2000);
    loop.exec();
    return refreshed;
}

}

TEST_CASE("Actual QML controls mesh cell types and retains counts, state and expansion", "[QML][visibility][mesh-types]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    auto component = std::make_unique<ComponentData>();
    component->mesh = std::make_unique<MeshData>();
    auto& mesh = *component->mesh;
    mesh.vertex_positions_ = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
        { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
    mesh.vertex_count_ = 8;
    mesh.edge_vertices_ = { 0, 1 };
    mesh.face_vertices_ = { 0, 1, 2, 0, 1, 2, 3 };
    mesh.face_vertices_offset_ = { 0, 3, 7 };
    mesh.solid_types_ = { 12 };
    mesh.solid_vertices_ = { 0, 1, 2, 3, 4, 5, 6, 7 };
    mesh.solid_vertices_offset_ = { 0, 8 };
    mesh.solid_faces_offset_ = { 0, 0 };
    ComponentDatas components;
    components.push_back(std::move(component));
    const auto model_id = model.addModel("mixed types", std::move(components));
    const auto id = model.modelById(model_id)->componentIds().front();
    auto* tree = f.tree->findChild<TreeModel*>();
    auto* viewport = f.viewport->findChild<QRenderWindow*>();
    REQUIRE(tree);
    REQUIRE(viewport);
    REQUIRE(tree->refresh());
    // 普通摘要只提供轻量总量；类型列表必须通过详细查询获取。
    const auto summary = f.manager->getModelQuery()->getMeshSummary(id);
    CHECK(summary["face_count"].toInt() == 2);
    CHECK_FALSE(summary.contains("face_types"));
    CHECK_FALSE(summary.contains("solid_types"));
    // 没有管线接收请求时，模型、组件、网格、维度和类型均不能乐观修改自身状态。
    TreeModel pending_tree;
    pending_tree.setModelQuery(f.manager->getModelQuery());
    REQUIRE(pending_tree.refresh());
    int pending_requests = 0;
    QObject::connect(&pending_tree, &TreeModel::visibilityRequested, &pending_tree,
        [&](int, int, bool) { ++pending_requests; });
    QObject::connect(&pending_tree, &TreeModel::meshCellVisibilityRequested, &pending_tree,
        [&](int, int, int, bool) { ++pending_requests; });
    auto pending_index = pending_tree.index(0, 0);
    for (int depth = 0; depth < 5; ++depth) {
        REQUIRE(pending_index.isValid());
        REQUIRE(pending_tree.requestVisibility(pending_index, false));
        CHECK(pending_tree.data(pending_index, TreeModel::VisibilityStateRole).toInt() == Qt::Checked);
        pending_index = pending_tree.index(0, 0, pending_index);
    }
    pending_tree.requestAllVisibility(false);
    CHECK(pending_requests == 6);
    pending_tree.syncComponentVisibility(id, false, false, { }, { });
    CHECK_FALSE(pending_tree.data(pending_tree.index(0, 0), TreeModel::IsVisibleRole).toBool());
    pending_tree.requestAllVisibility(true);
    CHECK_FALSE(pending_tree.data(pending_tree.index(0, 0), TreeModel::IsVisibleRole).toBool());
    // 首次建树前收到的完整状态必须同时保留总开关和类型过滤。
    TreeModel early_tree;
    early_tree.setModelQuery(f.manager->getModelQuery());
    early_tree.syncComponentVisibility(id, true, false, { 5 }, { });
    REQUIRE(early_tree.refresh());
    const auto early_mesh = early_tree.index(0, 0, early_tree.findIndexByNodeId(id, 1));
    const auto early_faces = early_tree.index(0, 0, early_mesh);
    CHECK_FALSE(early_tree.data(early_tree.index(0, 0, early_faces), TreeModel::IsVisibleRole).toBool());
    CHECK(early_tree.data(early_mesh, TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    // 每次用语义层级重新定位，不能跨模型重建继续持有旧 QModelIndex。
    const auto face_group = [&] { return tree->index(0, 0, tree->index(0, 0, tree->findIndexByNodeId(id, 1))); };
    auto faces = face_group();
    REQUIRE(tree->rowCount(faces) == 2);
    auto triangle = tree->index(0, 0, faces);
    CHECK(tree->data(triangle, TreeModel::NameRole).toString() == "三角形");
    CHECK(tree->data(triangle, TreeModel::NumberRole).toString() == "1");
    CHECK(tree->data(triangle, TreeModel::CanToggleVisibilityRole).toBool());
    const auto stable_key = tree->data(triangle, TreeModel::StableKeyRole);
    {
        // 第一次展示通知就应包含类型过滤，且组件不能因拆分回传而重复通知。
        const auto component_index = tree->findIndexByNodeId(id, 1);
        int component_updates = 0;
        const auto connection = QObject::connect(tree, &QAbstractItemModel::dataChanged, tree,
            [&](const QModelIndex& first, const QModelIndex&, const QList<int>&) {
                if (first == component_index) {
                    ++component_updates;
                    CHECK_FALSE(tree->data(triangle, TreeModel::IsVisibleRole).toBool());
                    CHECK(tree->data(faces, TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
                }
            });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
        REQUIRE(tree->requestVisibility(triangle, false));
        CHECK(component_updates == 1);
    }
    CHECK(tree->data(faces, TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    CHECK(tree->data(faces, TreeModel::NumberRole).toString() == "2");

    QQuickWindow host;
    host.resize(360, 600);
    auto* tree_page = qobject_cast<QQuickItem*>(f.tree.get());
    tree_page->setParentItem(host.contentItem());
    tree_page->setSize(QSizeF(360, 600));
    const auto cleanup = qScopeGuard([&] { host.hide(); tree_page->setParentItem(nullptr); });
    host.show();
    QCoreApplication::processEvents();
    auto* view = f.tree->findChild<QObject*>("objectTreeView");
    REQUIRE(view);
    REQUIRE(QMetaObject::invokeMethod(view, "expandRecursively", Q_ARG(int, -1), Q_ARG(int, -1)));
    REQUIRE(QMetaObject::invokeMethod(view, "forceLayout"));
    const int rows = view->property("rows").toInt();
    REQUIRE(rows > 5);
    REQUIRE(tree->refresh());
    QCoreApplication::processEvents();
    REQUIRE(QMetaObject::invokeMethod(view, "forceLayout"));
    CHECK(view->property("rows").toInt() == rows);
    faces = face_group();
    triangle = tree->index(0, 0, faces);
    CHECK(tree->data(triangle, TreeModel::StableKeyRole) == stable_key);
    CHECK_FALSE(tree->data(triangle, TreeModel::IsVisibleRole).toBool());

    // 维度批量操作和视口“显示全部”都必须回传正确的叶节点状态。
    REQUIRE(tree->requestVisibility(faces, false));
    CHECK(tree->data(faces, TreeModel::VisibilityStateRole).toInt() == Qt::Unchecked);
    REQUIRE(tree->requestVisibility(triangle, true));
    CHECK(tree->data(faces, TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    viewport->showAll();
    CHECK(tree->data(faces, TreeModel::VisibilityStateRole).toInt() == Qt::Checked);
    viewport->setMeshVisibility(id, false);
    REQUIRE(tree->requestVisibility(triangle, true));
    CHECK(tree->data(faces, TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    const auto volumes = tree->index(1, 0, tree->parent(faces));
    CHECK(tree->data(volumes, TreeModel::VisibilityStateRole).toInt() == Qt::Unchecked);
    viewport->showAll();
    // 两个维度全隐时，独立线仍由 Mesh 总开关控制，父级必须显示部分状态。
    viewport->setMeshCellVisibility(id, 2, -1, false);
    viewport->setMeshCellVisibility(id, 3, -1, false);
    CHECK(tree->data(tree->parent(faces), TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    CHECK(tree->data(tree->findIndexByNodeId(id, 1), TreeModel::VisibilityStateRole).toInt() == Qt::PartiallyChecked);
    viewport->setMeshVisibility(id, false);
    CHECK(tree->data(tree->parent(faces), TreeModel::VisibilityStateRole).toInt() == Qt::Unchecked);
    viewport->showAll();
    REQUIRE(tree->requestVisibility(triangle, false));
    model.markComponentDirty(id);
    mesh.face_vertices_ = { 0, 1, 2, 3, 0, 1, 2, 3 };
    mesh.face_vertices_offset_ = { 0, 4, 8 };
    model.flushNotifications();
    REQUIRE(waitForRefresh(*tree));
    faces = face_group();
    REQUIRE(tree->rowCount(faces) == 1);
    const auto quad = tree->index(0, 0, faces);
    CHECK(tree->data(quad, TreeModel::NameRole).toString() == "四边形");
    CHECK(tree->data(quad, TreeModel::NumberRole).toString() == "2");
    CHECK(tree->data(quad, TreeModel::IsVisibleRole).toBool());
}

TEST_CASE("Actual QML synchronizes reversed mesh and geometry visibility across tree refresh", "[QML][visibility]")
{
    application();
    QmlFixture f;
    auto& model = *f.manager->getModelManager();
    model.setUndoRecorder(nullptr);
    ComponentDatas components;
    auto component = std::make_unique<ComponentData>();
    bool empty_component = false;
    SECTION("mesh and geometry")
    {
        component->mesh = std::make_unique<MeshData>();
        component->geometry = std::make_unique<GeometryData>();
    }
    SECTION("empty component")
    {
        empty_component = true;
    }
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
            Q_ARG(Index, component_id), Q_ARG(bool, mesh_visible), Q_ARG(bool, geometry_visible),
            Q_ARG(QVariantList, QVariantList { }), Q_ARG(QVariantList, QVariantList { })));
        for (int round = 0; round < 2; ++round) {
            const auto index = tree->findIndexByNodeId(component_id, 1);
            REQUIRE(index.isValid());
            CHECK(tree->data(index, TreeModel::IsVisibleRole).toBool() == (mesh_visible || geometry_visible));
            CHECK(tree->data(tree->parent(index), TreeModel::IsVisibleRole).toBool() == (mesh_visible || geometry_visible));
            REQUIRE(tree->rowCount(index) == (empty_component ? 0 : 2));
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
    bool isolate = false;
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
    SECTION("object tree component isolation")
    {
        isolate = true;
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "isolateNode", Q_ARG(QVariant, id)));
    }
    SECTION("object tree mesh isolation")
    {
        isolate = true;
        const auto mesh_index = tree->index(0, 0, tree->findIndexByNodeId(id, 1));
        REQUIRE(QMetaObject::invokeMethod(f.tree.get(), "isolateSubNode",
            Q_ARG(QVariant, tree->data(mesh_index, TreeModel::NodeIdRole)),
            Q_ARG(QVariant, static_cast<int>(TreeNode::NodeType::Mesh)), Q_ARG(QVariant, id)));
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
        CHECK(notifications.contains(component_id) == (expected || isolate));
        if (expected || isolate)
            CHECK(notifications.at(component_id) == std::make_pair(expected, false));
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
    CHECK(selection_state->property("activeModelId").toInt() == mid);
    CHECK(selection_state->property("activeComponentId").toInt() == remaining_cid);
    REQUIRE(waitForRefresh(*tree_model));
    CHECK(selection_state->property("activeModelId").toInt() == mid);
    CHECK(selection_state->property("activeComponentId").toInt() == remaining_cid);
    // 删除最后一个组件仍保留模型选择。
    model.removeComponent(remaining_cid);
    CHECK(selection_state->property("activeModelId").toInt() == mid);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    REQUIRE(waitForRefresh(*tree_model));
    CHECK(selection_state->property("activeModelId").toInt() == mid);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    model.removeModel(mid);
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
    REQUIRE(waitForRefresh(*tree_model));
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
    REQUIRE(waitForRefresh(*tree_model));
    CHECK(selection_state->property("activeModelId").toInt() == -1);
    CHECK(selection_state->property("activeComponentId").toInt() == -1);
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
