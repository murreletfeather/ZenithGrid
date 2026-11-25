/**
 * @file TestSessionPlugins.cpp
 * @brief Session 插件装载冒烟测试：动态插件注册与功能信息（Qt 插件机制，需 QCoreApplication）
 *
 * 插件目录经编译期宏 PRECESS_PLUGIN_DIR 注入（构建树 plugins 目录），
 * 用以验证 Session 的系统装配与插件注册链路端到端可用。
 */
#include "Session.h"

#include "FeatureInfo.h"
#include "FeatureParams.h"
#include "FeatureSystem.h"
#include "JobRunner.h"
#include "Selection.h"
#include "SystemPluginManager.h"
#include "UndoStack.h"
#include "test/OwnerQueue.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

using namespace session;

namespace {
//! @brief OBJ 文件功能应发布的面组号属性名。
constexpr char expected_group_attribute_name[] = "xxx";

std::filesystem::path findPlugin(const std::filesystem::path& directory, const std::string& name)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto& path = entry.path();
        const auto extension = path.extension().string();
        if (path.stem().string().ends_with(name)
            && (extension == ".dll" || extension == ".so" || extension == ".dylib"))
            return path;
    }
    return { };
}

int static_standard_attempts = 0;
int static_unknown_attempts = 0;
QObject* failStaticStandardConstruction()
{
    ++static_standard_attempts;
    throw std::runtime_error("expected static plugin construction failure");
}
QObject* failStaticUnknownConstruction()
{
    ++static_unknown_attempts;
    throw 42;
}
}

TEST_CASE("Session loads plugins and registers feature infos", "[session][plugins]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);

#ifdef PRECESS_PLUGIN_DIR
    const std::filesystem::path plugin_dir(PRECESS_PLUGIN_DIR);
#else
    SKIP("PRECESS_PLUGIN_DIR not defined");
#endif

    Session s;
    s.loadStaticPlugins();
    s.loadPluginsFromDirectory(plugin_dir);

    SECTION("动态插件注册进对应系统")
    {
        const auto plugin_names = s.pluginManager().getPluginNames();
        CHECK(plugin_names.size() >= 12);
    }

    SECTION("功能插件经声明链暴露菜单贡献")
    {
        const auto& infos = s.featureSystem().getFeatureInfos();
        CHECK(infos.size() >= 12);
        bool has_geometry_menu = false;
        for (const systems::feature::FeatureInfo* info : infos) {
            for (const auto& menu : info->navigation.entries()) {
                if (menu.menu_id == "geometry")
                    has_geometry_menu = true;
            }
        }
        CHECK(has_geometry_menu);
    }

    SECTION("OBJ 文件功能提供分组选项并导入面属性")
    {
        auto& features = s.featureSystem();
        const std::string importing = "Import.OBJ";
        const auto* params = features.params(importing);
        REQUIRE(params);
        REQUIRE(params->count() == 2);
        CHECK(params->types()[0].type == ArgTypeEnum::Paths);
        CHECK(params->types()[1].type == ArgTypeEnum::Bool);
        const auto* split_groups = params->value(1).get<ArgTypeEnum::Bool>();
        REQUIRE(split_groups);
        CHECK_FALSE(*split_groups);

        // 两组共享点，切换开关后只改变组件划分。
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        QFile obj(directory.filePath(QStringLiteral("face_groups.obj")));
        REQUIRE(obj.open(QIODevice::WriteOnly));
        REQUIRE(obj.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
                          "g xxx123\nf 1 2 3\ng wall\nf 1 2 4\n") > 0);
        obj.close();
        REQUIRE(features.setParameter(importing, 0,
            core::ArgObject::create<ArgTypeEnum::Paths>({ QFileInfo(obj).filesystemFilePath() })));

        CHECK_FALSE(features.invoke(importing).has_value());
        const auto merged = s.query().listModels();
        REQUIRE(merged.size() == 1);
        CHECK(merged.front().component_count == 1);
        const auto merged_components = s.query().componentSummaries(merged.front().model_id);
        REQUIRE(merged_components.size() == 1);
        const auto* component = s.model().findComponent(merged_components.front().component_id);
        REQUIRE(component);
        REQUIRE(component->mesh);
        CHECK(component->mesh->vertex_count_ == 4);
        CHECK(component->mesh->face_attributes_.at(expected_group_attribute_name) == std::vector<double> { 123.0, 0.0 });
        const auto attributes = s.query().componentAttributeInfos(merged_components.front().component_id);
        REQUIRE(attributes.size() == 1);
        CHECK(attributes.front().name == expected_group_attribute_name);
        CHECK(attributes.front().type == ElementEnum::Type::Face);
        CHECK(attributes.front().component_count == 1);
        REQUIRE(s.undoStack().undo());
        CHECK(s.query().listModels().empty());

        REQUIRE(features.setParameter(importing, 1, core::ArgObject::create<ArgTypeEnum::Bool>(true)));
        CHECK_FALSE(features.invoke(importing).has_value());
        const auto split = s.query().listModels();
        REQUIRE(split.size() == 1);
        CHECK(split.front().component_count == 2);
    }
}

TEST_CASE("Busy plugin unload keeps feature registration until a successful retry", "[session][plugins][job]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
#ifdef PRECESS_PLUGIN_DIR
    const std::filesystem::path plugin_dir(PRECESS_PLUGIN_DIR);
#else
    SKIP("PRECESS_PLUGIN_DIR not defined");
#endif
    std::string plugin_fragment;
    // 使用默认构建的产品插件，不能依赖 PRECESS_BUILD_EXAMPLES=ON。
    SECTION("feature") { plugin_fragment = "CreatePointPlugin"; }
    SECTION("file import") { plugin_fragment = "OffImportPlugin"; }
    SECTION("file export") { plugin_fragment = "OffExportPlugin"; }
    const auto plugin_path = findPlugin(plugin_dir, plugin_fragment);
    REQUIRE_FALSE(plugin_path.empty());
    OwnerQueue queue;
    Session session(nullptr, queue.dispatcher());
    REQUIRE(session.pluginManager().registerPlugin(plugin_path));
    const auto names = session.pluginManager().getPluginNames();
    const auto infos = session.featureSystem().getFeatureInfos();
    REQUIRE(names.size() == 1);
    REQUIRE(infos.size() == 1);
    auto job = session.jobRunner()->run("pure", [] {
        return systems::job::JobWork { [](systems::job::ProgressFn) { } };
    });
    REQUIRE(job);
    auto complete = queue.take();
    REQUIRE_THROWS_AS(session.pluginManager().unregisterPlugin(plugin_path), ModelOperationBusy);
    REQUIRE(session.pluginManager().getPluginNames() == names);
    REQUIRE(session.featureSystem().getFeatureInfos() == infos);
    REQUIRE_FALSE(job->isCancellationRequested());
    complete();
    REQUIRE(job->state() == systems::job::JobState::Done);
    REQUIRE_NOTHROW(session.pluginManager().unregisterPlugin(plugin_path));
    REQUIRE(session.pluginManager().getPluginNames().empty());
    REQUIRE(session.featureSystem().getFeatureInfos().empty());
}

TEST_CASE("Separate OFF plugins import and export through Feature jobs", "[session][plugins][file]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    const std::filesystem::path plugin_dir(PRECESS_PLUGIN_DIR);
    const auto reader = findPlugin(plugin_dir, "OffImportPlugin");
    const auto writer = findPlugin(plugin_dir, "OffExportPlugin");
    REQUIRE_FALSE(reader.empty());
    REQUIRE_FALSE(writer.empty());

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QFile off(directory.filePath(QStringLiteral("三角形.off")));
    REQUIRE(off.open(QIODevice::WriteOnly));
    REQUIRE(off.write("OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n") > 0);
    off.close();
    const auto input = QFileInfo(off).filesystemFilePath();
    const auto output_name = directory.filePath(QStringLiteral("复制.off"));
    const auto output = QFileInfo(output_name).filesystemFilePath();

    OwnerQueue queue;
    Session session(nullptr, queue.dispatcher());
    auto& features = session.featureSystem();
    using systems::feature::FileDirection;
    using systems::job::Job;
    using systems::job::JobState;
    REQUIRE(session.pluginManager().registerPlugin(reader));
    REQUIRE(features.getFileTypes(FileDirection::Import).size() == 1);
    CHECK(features.getFileTypes(FileDirection::Export).empty());
    REQUIRE(session.pluginManager().registerPlugin(writer));
    REQUIRE(features.getFileTypes(FileDirection::Export).size() == 1);

    SECTION("导入成功后可导出，且导出没有新增 undo 记录")
    {
        REQUIRE(features.setParameter("Import.Off", 0, core::ArgObject::create<ArgTypeEnum::Paths>({ input, input })));
        auto imported = std::any_cast<std::shared_ptr<Job>>(features.invoke("Import.Off"));
        REQUIRE(imported);
        auto finish_import = queue.take();
        CHECK(session.query().listModels().empty());
        finish_import();
        REQUIRE(imported->state() == JobState::Done);
        const auto models = session.query().listModels();
        REQUIRE(models.size() == 2);
        auto selection = std::make_shared<Selection>();
        selection->type = ElementEnum::Component;
        selection->ids = { session.query().componentSummaries(models.front().model_id).front().component_id };
        REQUIRE(features.setParameter("Export.Off", 0, core::ArgObject::create<ArgTypeEnum::Path>(output)));
        REQUIRE(features.setParameter("Export.Off", 1, core::ArgObject::create<ArgTypeEnum::Selector>(selection)));
        auto exported = std::any_cast<std::shared_ptr<Job>>(features.invoke("Export.Off"));
        REQUIRE(exported);
        queue.take()();
        REQUIRE(exported->state() == JobState::Done);
        QFile result(output_name);
        REQUIRE(result.open(QIODevice::ReadOnly));
        CHECK(result.readAll().startsWith("OFF"));

        session.pluginManager().unregisterPlugin(reader);
        CHECK(features.getFileTypes(FileDirection::Import).empty());
        CHECK(features.getFileTypes(FileDirection::Export).size() == 1);
        REQUIRE(session.undoStack().undo());
        CHECK(session.query().listModels().empty());
        CHECK_FALSE(session.undoStack().canUndo());
    }

    SECTION("批量导入跳过坏文件，成功项一次撤销")
    {
        REQUIRE(features.setParameter("Import.Off", 0, core::ArgObject::create<ArgTypeEnum::Paths>({ input, output, input })));
        auto imported = std::any_cast<std::shared_ptr<Job>>(features.invoke("Import.Off"));
        REQUIRE(imported);
        queue.take()();
        REQUIRE(imported->state() == JobState::Done);
        REQUIRE(session.query().listModels().size() == 2);
        REQUIRE(session.undoStack().undo());
        CHECK(session.query().listModels().empty());
        CHECK_FALSE(session.undoStack().canUndo());
    }

    SECTION("全部文件解析失败时报告任务失败")
    {
        REQUIRE(features.setParameter("Import.Off", 0, core::ArgObject::create<ArgTypeEnum::Paths>({ output })));
        auto imported = std::any_cast<std::shared_ptr<Job>>(features.invoke("Import.Off"));
        REQUIRE(imported);
        queue.take()();
        CHECK(imported->state() == JobState::Failed);
        CHECK(session.query().listModels().empty());
        CHECK_FALSE(session.undoStack().canUndo());
    }

    SECTION("原生调用传入错型或空参数时拒绝执行")
    {
        using core::ArgObject;
        REQUIRE(features.setParameter("Import.Off", 0, ArgObject::create<ArgTypeEnum::Bool>(true)));
        CHECK(std::any_cast<std::string>(features.invoke("Import.Off")) == "请选择文件路径。");
        REQUIRE(features.setParameter("Import.Off", 0, { }));
        CHECK(std::any_cast<std::string>(features.invoke("Import.Off")) == "请选择文件路径。");

        REQUIRE(features.setParameter("Export.Off", 0, { }));
        CHECK(std::any_cast<std::string>(features.invoke("Export.Off")) == "请选择文件路径。");
        REQUIRE(features.setParameter("Export.Off", 0, ArgObject::create<ArgTypeEnum::Path>(output)));
        REQUIRE(features.setParameter("Export.Off", 1, ArgObject::create<ArgTypeEnum::Bool>(true)));
        CHECK(std::any_cast<std::string>(features.invoke("Export.Off")) == "请选择要导出的组件。");
        REQUIRE(features.setParameter("Export.Off", 1, ArgObject::create<ArgTypeEnum::Selector>({ })));
        CHECK(std::any_cast<std::string>(features.invoke("Export.Off")) == "请选择要导出的组件。");
        CHECK_FALSE(session.jobRunner()->currentJob());
        CHECK_FALSE(QFile::exists(output_name));
    }

    SECTION("导入提交前取消时丢弃解析结果")
    {
        REQUIRE(features.setParameter("Import.Off", 0, core::ArgObject::create<ArgTypeEnum::Paths>({ input })));
        auto imported = std::any_cast<std::shared_ptr<Job>>(features.invoke("Import.Off"));
        REQUIRE(imported);
        auto finish_import = queue.take();
        imported->cancel();
        finish_import();
        CHECK(imported->state() == JobState::Cancelled);
        CHECK(session.query().listModels().empty());
        CHECK_FALSE(session.undoStack().canUndo());
    }
}

TEST_CASE("Binary feature setup supplies navigation and survives reload", "[session][plugins][setup][navigation]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    const std::filesystem::path source(PRECESS_SETUP_TEST_PLUGIN);
    REQUIRE(std::filesystem::exists(source));
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto target = std::filesystem::path(directory.path().toStdString()) / source.filename();
    REQUIRE(QFile::copy(QString::fromStdString(source.string()), QString::fromStdString(target.string())));
    SECTION("Minimal identity JSON") { }
    SECTION("Conflicting legacy sidecar cannot override setup")
    {
        auto sidecar = target;
        sidecar.replace_extension(".navigation.json");
        QFile declaration(QString::fromStdString(sidecar.string()));
        REQUIRE(declaration.open(QIODevice::WriteOnly));
        REQUIRE(declaration.write(R"({"categories":["triangle"],"label":"Legacy label"})") > 0);
    }
    Session session;
    for (int reload = 0; reload < 2; ++reload) {
        REQUIRE(session.pluginManager().registerPlugin(target));
        const auto infos = session.featureSystem().getFeatureInfos();
        REQUIRE(infos.size() == 1);
        CHECK(infos.front()->name == "setupNavigationTest");
        CHECK(infos.front()->navigation.label() == "Setup feature");
        CHECK(infos.front()->navigation.entryIds() == std::vector<std::string> { "setup-custom" });
        CHECK(infos.front()->navigation.order() == 7);
        CHECK(infos.front()->navigation.entryDefaults().at("setup-custom").at("Size") == "3");
        REQUIRE(infos.front()->arg_types.size() == 2);
        CHECK(infos.front()->arg_types.front().type == ArgTypeEnum::Paths);
        CHECK(infos.front()->arg_types[1].type == ArgTypeEnum::Float);
        CHECK(infos.front()->arg_types[1].name == "Size");
        const auto files = session.featureSystem().getFileTypes(systems::feature::FileDirection::Import);
        REQUIRE(files.size() == 1);
        CHECK(files.front().feature == "setupNavigationTest");
        CHECK(files.front().display_name == infos.front()->display_name);
        CHECK(files.front().extensions == std::vector<std::string> { "dat" });
        const auto categories = session.featureSystem().getNavigationEntries();
        REQUIRE(categories.size() == 1);
        CHECK(categories.front().title == "Setup custom category");
        CHECK(categories.front().order == 5);
        CHECK(categories.front().menu_id == "setup-tools");
        CHECK(categories.front().group_id == "custom");
        const auto menus = session.featureSystem().getNavigationMenus();
        REQUIRE(menus.size() == 1);
        CHECK(menus.front().id == "setup-tools");
        CHECK(menus.front().title == "Tools");
        const auto groups = session.featureSystem().getNavigationGroups();
        REQUIRE(groups.size() == 1);
        CHECK(groups.front().menu_id == "setup-tools");
        CHECK(groups.front().id == "custom");
        CHECK(groups.front().title == "Custom");
        session.pluginManager().unregisterPlugin(target);
        CHECK(session.featureSystem().getFeatureInfos().empty());
        CHECK(session.featureSystem().getNavigationEntries().empty());
        CHECK(session.featureSystem().getFileTypes(systems::feature::FileDirection::Import).empty());
        CHECK(session.featureSystem().getNavigationMenus() == menus);
        CHECK(session.featureSystem().getNavigationGroups() == groups);
    }
}

TEST_CASE("Directory plugin loading continues after registration exceptions and permits retry", "[session][plugins][failure]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    const std::filesystem::path source(PRECESS_SETUP_TEST_PLUGIN);
    REQUIRE(std::filesystem::exists(source));
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto plugin_dir = std::filesystem::path(directory.path().toStdString());
    const auto first = plugin_dir / ("first" + source.extension().string());
    const auto second = plugin_dir / ("second" + source.extension().string());
    for (const auto& target : { first, second })
        REQUIRE(QFile::copy(QString::fromStdString(source.string()), QString::fromStdString(target.string())));

    class FailingOnceRegister : public systems::SystemRegisterBase {
    public:
        bool unknown_exception = false;
        int attempts = 0;
        bool registerPlugin(const QJsonObject&, systems::PluginBase&) override
        {
            if (++attempts == 1) {
                if (unknown_exception)
                    throw 42;
                throw std::runtime_error("expected registration failure");
            }
            return true;
        }
        void unregisterPlugin(const QJsonObject&) override { }
    };
    auto registrar = std::make_unique<FailingOnceRegister>();
    auto* probe = registrar.get();
    SECTION("standard exception") { }
    SECTION("unknown exception") { probe->unknown_exception = true; }
    Session session;
    auto& manager = session.pluginManager();
    manager.removeSystemRegister(systems::feature::FeatureSystem::name);
    REQUIRE(manager.addSystemRegister(systems::feature::FeatureSystem::name, std::move(registrar)));
    REQUIRE_NOTHROW(session.loadPluginsFromDirectory(plugin_dir));
    CHECK(probe->attempts == 2);
    const auto names = manager.getPluginNames();
    REQUIRE(names.size() == 1);
    const auto& rejected = names.front() == first.stem().string() ? second : first;
    REQUIRE(manager.registerPlugin(rejected));
    CHECK(probe->attempts == 3);
    CHECK(manager.getPluginNames().size() == 2);
}

TEST_CASE("Dynamic plugin construction exceptions return false and permit retry", "[session][plugins][failure]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    const std::filesystem::path source(PRECESS_SETUP_TEST_PLUGIN);
    for (const auto& failure : { QByteArray("standard"), QByteArray("unknown") }) {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const auto target = std::filesystem::path(directory.path().toStdString()) / source.filename();
        REQUIRE(QFile::copy(QString::fromStdString(source.string()), QString::fromStdString(target.string())));
        const auto previous = qgetenv("PRECESS_TEST_SETUP_CONSTRUCTION");
        const auto restore = qScopeGuard([previous] {
            if (previous.isNull())
                qunsetenv("PRECESS_TEST_SETUP_CONSTRUCTION");
            else
                qputenv("PRECESS_TEST_SETUP_CONSTRUCTION", previous);
        });
        Session session;
        qputenv("PRECESS_TEST_SETUP_CONSTRUCTION", failure);
        REQUIRE_FALSE(session.pluginManager().registerPlugin(target));
        CHECK(session.pluginManager().getPluginNames().empty());
        CHECK(session.featureSystem().getFeatureInfos().empty());
        qunsetenv("PRECESS_TEST_SETUP_CONSTRUCTION");
        REQUIRE(session.pluginManager().registerPlugin(target));
        CHECK(session.featureSystem().getFeatureInfos().size() == 1);
    }
}

TEST_CASE("Static plugin construction exceptions do not stop later plugins", "[session][plugins][failure]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    QLibrary library(QString::fromUtf8(PRECESS_SETUP_TEST_PLUGIN));
    library.setLoadHints(QLibrary::PreventUnloadHint);
    REQUIRE(library.load());
    const auto metadata = reinterpret_cast<QtPluginMetaDataFunction>(library.resolve("qt_plugin_query_metadata_v2"));
    const auto instance = reinterpret_cast<QtPluginInstanceFunction>(library.resolve("qt_plugin_instance"));
    REQUIRE(metadata);
    REQUIRE(instance);
    static_standard_attempts = 0;
    static_unknown_attempts = 0;
    // 工厂与元数据来自同一测试 DLL，PreventUnload 保证全局静态注册表中的指针有效。
    qRegisterStaticPluginFunction(QStaticPlugin(failStaticStandardConstruction, metadata));
    qRegisterStaticPluginFunction(QStaticPlugin(failStaticUnknownConstruction, metadata));
    qRegisterStaticPluginFunction(QStaticPlugin(instance, metadata));
    Session session;
    REQUIRE_NOTHROW(session.loadStaticPlugins());
    CHECK(static_standard_attempts == 1);
    CHECK(static_unknown_attempts == 1);
    CHECK(session.pluginManager().getPluginNames().size() == 1);
    CHECK(session.featureSystem().getFeatureInfos().size() == 1);
}
