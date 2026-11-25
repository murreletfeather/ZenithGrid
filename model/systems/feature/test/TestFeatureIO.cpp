/** @file TestFeatureIO.cpp
 * @brief 文件 Feature 的声明、参数、导入记账与导出回归。
 */
#include "ComponentData.h"
#include "FeatureContext.h"
#include "FeatureHandler.h"
#include "FeatureRegistrar.h"
#include "FeatureSystem.h"
#include "MeshData.h"
#include "ModelData.h"
#include "ModelLayer.h"
#include "ModelObserver.h"
#include "Selection.h"
#include "UndoStack.h"
#include <catch2/catch_test_macros.hpp>
#include <functional>

using namespace systems::feature;
namespace {
class ImportModelObserver final : public ModelObserver {
public:
    Index last_added { -1 };
    std::vector<Index> added;
    void notifyModelAdded(Index id) override
    {
        last_added = id;
        added.push_back(id);
    }
    void notifyModelChanged(Index) override { }
    void notifyComponentChanged(Index) override { }
    void notifyModelRemoved(Index) override { }
    void notifyComponentRemoved(Index) override { }
    void notifyModelNameChanged(Index, const std::string&) override { }
    void notifyGeometryLoadFailed(const std::string&) override { }
};

class NativeFileFeature final : public FeatureHandler {
public:
    explicit NativeFileFeature(FileTypeDeclaration declared)
        : declared_(std::move(declared))
    {
    }
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        registrar.setFileType(declared_);
        registrar.addParameter({ ArgTypeEnum::Path, "文件", "", "" });
        if (declared_.direction == FileDirection::Export)
            registrar.addParameter({ ArgTypeEnum::Selector, "组件", "Component", "" });
        registrar.addParameter({ ArgTypeEnum::Bool, "Binary", "false", "" });
    }
    std::function<void(FeatureContext&)> action;
    std::any execute(FeatureContext& context) override
    {
        if (action)
            action(context);
        return { };
    }

private:
    FileTypeDeclaration declared_;
};
Index addTriangleComponent(ModelLayer& model, const std::string& name = "Target")
{
    auto component = std::make_unique<ComponentData>();
    component->name = name;
    component->mesh = std::make_unique<MeshData>();
    component->mesh->init();
    component->mesh->vertex_positions_ = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    ComponentDatas components;
    components.push_back(std::move(component));
    const auto id = model.addModel(name, std::move(components));
    return model.modelById(id)->componentIds().front();
}
std::size_t pointCount(const ModelLayer& model, Index id)
{
    return model.findComponent(id)->mesh->vertex_positions_.size();
}
}
TEST_CASE("File features declare parameters and use feature execution", "[FeatureSystem][file]")
{
    core::EventBus bus;
    ImportModelObserver observer;
    ModelLayer model(&observer);
    const auto target = addTriangleComponent(model);
    UndoStack undo(model);
    model.setUndoRecorder(&undo);
    FeatureSystem system(model, bus, &undo);
    HandlerMetaData importing;
    importing.name = "NativeImport";
    auto reader = std::make_unique<NativeFileFeature>(FileTypeDeclaration { FileDirection::Import, { "dat" } });
    reader->action = [&](FeatureContext& context) {
        CHECK(*context.params.value(0).get<ArgTypeEnum::Path>() == "input.dat");
        CHECK(*context.params.value(1).get<ArgTypeEnum::Bool>());
        context.model.addModel("native import", { });
    };
    REQUIRE(system.registerHandler(importing, FeatureSystem::SystemHandlerPtr { reader.release() }));
    REQUIRE(system.params(importing.name)->count() == 2);
    CHECK(system.params(importing.name)->types()[0].type == ArgTypeEnum::Path);
    REQUIRE(system.setParameter(importing.name, 0, core::ArgObject::create<ArgTypeEnum::Path>("input.dat")));
    REQUIRE(system.setParameter(importing.name, 1, core::ArgObject::create<ArgTypeEnum::Bool>(true)));
    REQUIRE(system.getFileTypes(FileDirection::Import).front().feature == importing.name);
    system.invoke(importing.name);
    const auto added = observer.last_added;
    REQUIRE(model.modelById(added));
    REQUIRE(undo.undo());
    CHECK_FALSE(model.modelById(added));
    REQUIRE(undo.canRedo());

    HandlerMetaData exporting;
    exporting.name = "NativeExport";
    auto writer = std::make_unique<NativeFileFeature>(FileTypeDeclaration { FileDirection::Export, { "dat" } });
    bool called = false;
    writer->action = [&](FeatureContext& context) {
        called = true;
        CHECK(*context.params.value(0).get<ArgTypeEnum::Path>() == "output.dat");
        CHECK((*context.params.value(1).get<ArgTypeEnum::Selector>())->ids == std::vector<Index> { target });
        CHECK(*context.params.value(2).get<ArgTypeEnum::Bool>());
        CHECK(pointCount(context.model, target) == 3);
    };
    REQUIRE(system.registerHandler(exporting, FeatureSystem::SystemHandlerPtr { writer.release() }));
    REQUIRE(system.params(exporting.name)->count() == 3);
    CHECK(system.params(exporting.name)->types()[1].type == ArgTypeEnum::Selector);
    CHECK(system.params(exporting.name)->types()[1].content == "Component");
    auto selected = std::make_shared<Selection>();
    selected->type = ElementEnum::Component;
    selected->ids = { target };
    REQUIRE(system.setParameter(exporting.name, 0, core::ArgObject::create<ArgTypeEnum::Path>("output.dat")));
    REQUIRE(system.setParameter(exporting.name, 1, core::ArgObject::create<ArgTypeEnum::Selector>(selected)));
    REQUIRE(system.setParameter(exporting.name, 2, core::ArgObject::create<ArgTypeEnum::Bool>(true)));
    const auto label = undo.undoLabel();
    const auto redo_label = undo.redoLabel();
    REQUIRE(system.getFileTypes(FileDirection::Export).front().feature == exporting.name);
    system.invoke(exporting.name);
    CHECK(called);
    CHECK_FALSE(undo.scopeActive());
    CHECK(undo.undoLabel() == label);
    CHECK(undo.redoLabel() == redo_label);
    CHECK(pointCount(model, target) == 3);
    CHECK(undo.canRedo());
    system.unregisterHandler(importing);
    CHECK(system.getFileTypes(FileDirection::Import).empty());
    CHECK(system.getFileTypes(FileDirection::Export).size() == 1);
    system.unregisterHandler(exporting);
    CHECK(system.getFileTypes(FileDirection::Export).empty());
}

TEST_CASE("File Features use their own IDs while sharing labels and extensions", "[FeatureSystem][file]")
{
    core::EventBus bus;
    ModelLayer model;
    FeatureSystem system(model, bus);
    const auto add = [&](const std::string& name) {
        HandlerMetaData metadata;
        metadata.name = name;
        metadata.display_name = "Same format";
        auto handler = std::make_unique<NativeFileFeature>(FileTypeDeclaration { FileDirection::Import, { "dat" } });
        return system.registerHandler(metadata, FeatureSystem::SystemHandlerPtr { handler.release() });
    };
    REQUIRE(add("A"));
    REQUIRE(add("B"));
    REQUIRE(add("A"));
    const auto files = system.getFileTypes(FileDirection::Import);
    REQUIRE(files.size() == 2);
    CHECK(files[0].feature == "A");
    CHECK(files[1].feature == "B");
    CHECK(files[0].display_name == files[1].display_name);
    CHECK(files[0].extensions == files[1].extensions);
    CHECK(system.params("dat") == nullptr);
}
