#include "ComponentData.h"
#include "EventBus.h"
#include "FeatureParams.h"
#include "FeatureSystem.h"
#include "GeometryBuilder.h"
#include "GeometryData.h"
#include "ModelLayer.h"
#include "ModelOperator.h"
#include "SplitEdgeHandler.h"
#include "UndoStack.h"
#include <BRep_Tool.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <any>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
using namespace systems;
using namespace systems::feature;
TEST_CASE("SplitEdge feature registers its baseline parameters", "[SplitEdgePlugin]")
{
    core::EventBus bus;
    ModelLayer model;
    FeatureSystem system(model, bus);
    HandlerMetaData meta;
    meta.name = "SplitEdge";
    FeatureSystem::SystemHandlerPtr handler { new SplitEdgeHandler };
    REQUIRE(system.registerHandler(meta, std::move(handler)));
    const auto navigation_entries = system.getNavigationEntries();
    REQUIRE(navigation_entries.size() == 1);
    CHECK(navigation_entries.front().id == "SplitEdge");
    CHECK(navigation_entries.front().icon == "qrc:/images/toolbar/Geometry/split-edge.svg");
    CHECK(navigation_entries.front().title == "分割边");
    CHECK(navigation_entries.front().menu_id == "geometry");
    CHECK(navigation_entries.front().group_id == "topology");
    REQUIRE(std::any_cast<std::string>(system.invoke("SplitEdge")) == "请在需要分割的几何边上选择一个分割点。");
}

TEST_CASE("SplitEdge derives its target from the precise point and splits the edge", "[SplitEdgePlugin][GeometryPoint]")
{
    core::EventBus bus;
    ModelLayer model;
    const TopoDS_Edge edge = TopoDS::Edge(GeometryBuilder::makeLine(0, 0, 0, 10, 0, 0));
    auto component = std::make_unique<ComponentData>();
    component->geometry = std::make_unique<GeometryData>();
    component->geometry->setRootShape(edge);
    const Index model_id = model.addModel("edge", { });
    auto op = model.getModelOperator(model_id);
    REQUIRE(op);
    const Index component_id = op->addGeometryComponent(std::move(component));
    auto* stored = model.findComponent(component_id);
    stored->geometry->ensureIndexBuilt(model.geomRegistry());
    const auto& index = stored->geometry->index;
    const auto edge_id = index.edgeGlobalId(index.type_maps[index.typeIndex(TopAbs_EDGE)].FindIndex(edge));
    GeometryPoint point;
    point.component_id = component_id;
    point.support_kind = GeometryPointKind::Edge;
    point.support_id = edge_id;
    point.world_pos = { 3.0, 0.0, 0.0 };
    point.edge_parameter = 3.0;
    point.valid = true;
    auto selection = std::make_shared<Selection>(Selection::create<ElementEnum::GeometryPoint>({ point }));
    UndoStack undo(model);
    model.setUndoRecorder(&undo);
    FeatureSystem system(model, bus, &undo);
    HandlerMetaData meta;
    meta.name = "SplitEdge";
    REQUIRE(system.registerHandler(meta, FeatureSystem::SystemHandlerPtr { new SplitEdgeHandler }));
    const TopoDS_Shape original = *stored->geometry->rootShape;
    REQUIRE(system.setParameter("SplitEdge", 0, core::ArgObject::create<ArgTypeEnum::Selector>(selection)));
    CHECK(original.IsSame(*stored->geometry->rootShape));
    CHECK_FALSE(undo.canUndo());
    REQUIRE(std::any_cast<std::string>(system.invoke("SplitEdge")) == "分割边成功");
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(*stored->geometry->rootShape, TopAbs_EDGE, edges);
    REQUIRE(edges.Extent() == 2);
    double first = 0.0, last = 0.0;
    BRep_Tool::Range(TopoDS::Edge(edges(1)), first, last);
    CHECK((first == Catch::Approx(point.edge_parameter) || last == Catch::Approx(point.edge_parameter)));
    // 原生执行保留参数，再次执行由后端拒绝已失效的边；失败不增加撤销记录。
    CHECK(std::any_cast<std::string>(system.invoke("SplitEdge")) == "所选几何边已失效。");
    REQUIRE(undo.undo());
    edges.Clear();
    TopExp::MapShapes(*model.findComponent(component_id)->geometry->rootShape, TopAbs_EDGE, edges);
    CHECK(edges.Extent() == 1);
    REQUIRE(undo.redo());
    edges.Clear();
    TopExp::MapShapes(*model.findComponent(component_id)->geometry->rootShape, TopAbs_EDGE, edges);
    CHECK(edges.Extent() == 2);
}
