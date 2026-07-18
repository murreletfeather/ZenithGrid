/**
 * @file TestMeasureHandler.cpp
 * @brief 测量处理器的单元测试
 */

#include "MeasureHandler.h"
#include "ArgObject.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "GeometryData.h"
#include "MakeMeshData.h"
#include "MeshData.h"
#include "ModelIOSystemBase.h"
#include "ModelLayer.h"
#include "Selection.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Shape.hxx>

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <string>

using namespace systems::algo;

namespace {

class MockModelIOSystem : public systems::io::ModelIOSystemBase {
public:
    void read(const std::filesystem::path&, const std::string&, const std::vector<std::any>&) override {}
    void write(Index, const std::filesystem::path&, const std::string&, const std::vector<std::any>&) override {}
    void writeComponents(const std::vector<Index>&, const std::filesystem::path&,
        const std::string&, const std::vector<std::any>&) override {}
};

std::shared_ptr<Selection> makeVertexSelection(const std::vector<Index>& ids)
{
    return std::make_shared<Selection>(Selection { ids, ElementEnum::Vertex, 0 });
}

std::shared_ptr<Selection> makeEdgeSelection(const std::vector<Index>& ids)
{
    return std::make_shared<Selection>(Selection { ids, ElementEnum::Edge, 0 });
}

std::shared_ptr<Selection> makeGeometrySelection(ElementEnum::Type type, const std::vector<Index>& ids)
{
    return std::make_shared<Selection>(Selection { ids, type, 0 });
}

//! @brief 在 10×10×10 的 OCC 立方体几何组件上执行一次测量，返回结果字符串
std::string executeGeometryMeasureOnBox(int measure_type_index, ElementEnum::Type sel_type, int local_id)
{
    BRepPrimAPI_MakeBox box_maker(10.0, 10.0, 10.0);
    box_maker.Build();
    if (!box_maker.IsDone())
        return {};

    auto geometry = std::make_unique<GeometryData>();
    geometry->rootShape = std::make_unique<TopoDS_Shape>(box_maker.Shape());

    auto c = std::make_unique<ComponentData>();
    c->id = -1;
    c->name = "box";
    c->geometry = std::move(geometry);

    ComponentDatas comps;
    comps.push_back(std::move(c));

    ModelLayer mgr;
    const Index model_id = mgr.addModel("box", std::move(comps));
    auto cids = mgr.modelById(model_id)->componentIds();
    if (cids.size() != 1)
        return {};

    ComponentData* comp = mgr.findComponent(cids[0]);
    if (!comp)
        return {};
    ComponentOperator comp_op(cids[0], *comp, mgr, nullptr);
    comp->geometry->ensureIndexBuilt(mgr.geomRegistry());

    Index gid = -1;
    switch (sel_type) {
    case ElementEnum::GeometryEdge:
        gid = comp->geometry->index.edgeGlobalId(local_id);
        break;
    case ElementEnum::GeometryFace:
        gid = comp->geometry->index.faceGlobalId(local_id);
        break;
    case ElementEnum::GeometrySolid:
        gid = comp->geometry->index.solidGlobalId(local_id);
        break;
    default:
        break;
    }

    core::ArgObject type_arg = core::ArgObject::create<ArgTypeEnum::Combo>(measure_type_index);
    auto selection = makeGeometrySelection(sel_type, { gid });
    selection->component_id = cids[0];
    core::ArgObject sel_arg = core::ArgObject::create<ArgTypeEnum::Selector>(selection);

    MockModelIOSystem io_sys;
    HandlerContext ctx { io_sys, comp_op };

    MeasureHandler handler;
    auto result_any = handler.execute(ctx, { type_arg, sel_arg });
    if (const std::string* result = std::any_cast<std::string>(&result_any))
        return *result;
    return {};
}

} // namespace

TEST_CASE("MeasureHandler args_type is correct")
{
    MeasureHandler handler;
    auto args = handler.args_type();
    REQUIRE(args.size() == 2);
    CHECK(args[0].type == ArgTypeEnum::Combo);
    CHECK(args[1].type == ArgTypeEnum::Selector);
}

TEST_CASE("MeasureHandler: distance between two vertices")
{
    auto mesh = std::make_unique<MeshData>(MakeMeshData());
    auto c = std::make_unique<ComponentData>();
    c->id = -1;
    c->name = "Comp";
    c->mesh = std::move(mesh);

    ComponentDatas comps;
    comps.push_back(std::move(c));

    ModelLayer mgr;
    const Index model_id = mgr.addModel("test", std::move(comps));

    auto cids = mgr.modelById(model_id)->componentIds();
    REQUIRE(cids.size() == 1);

    ComponentData* comp = mgr.findComponent(cids[0]);
    REQUIRE(comp);
    ComponentOperator comp_op(cids[0], *comp, mgr, nullptr);

    MeshData* mesh_ptr = comp->asMeshData();
    REQUIRE(mesh_ptr);
    const Index base = mesh_ptr->local_to_global_[0];

    core::ArgObject type_arg = core::ArgObject::create<ArgTypeEnum::Combo>(0); // 距离
    auto selection = makeVertexSelection({ base + 0, base + 1 });
    selection->component_id = cids[0];
    core::ArgObject sel_arg = core::ArgObject::create<ArgTypeEnum::Selector>(selection);

    MockModelIOSystem io_sys;
    HandlerContext ctx { io_sys, comp_op };

    MeasureHandler handler;
    auto result_any = handler.execute(ctx, { type_arg, sel_arg });
    const std::string* result = std::any_cast<std::string>(&result_any);
    REQUIRE(result != nullptr);
    CHECK(result->find("Distance") != std::string::npos);
    CHECK(result->find("1.000000") != std::string::npos);
}

TEST_CASE("MeasureHandler: length of edges selected as endpoint vertex id pairs")
{
    auto mesh = std::make_unique<MeshData>(MakeMeshData());
    auto c = std::make_unique<ComponentData>();
    c->id = -1;
    c->name = "Comp";
    c->mesh = std::move(mesh);

    ComponentDatas comps;
    comps.push_back(std::move(c));

    ModelLayer mgr;
    const Index model_id = mgr.addModel("test", std::move(comps));

    auto cids = mgr.modelById(model_id)->componentIds();
    REQUIRE(cids.size() == 1);

    ComponentData* comp = mgr.findComponent(cids[0]);
    REQUIRE(comp);
    ComponentOperator comp_op(cids[0], *comp, mgr, nullptr);

    MeshData* mesh_ptr = comp->asMeshData();
    REQUIRE(mesh_ptr);
    const Index base = mesh_ptr->local_to_global_[0];

    core::ArgObject type_arg = core::ArgObject::create<ArgTypeEnum::Combo>(3); // 长度
    // 边选择的 ids 是端点顶点 id 对：{12,13} 与 {0,1} 长度均为 1
    auto selection = makeEdgeSelection({ base + 12, base + 13, base + 0, base + 1 });
    selection->component_id = cids[0];
    core::ArgObject sel_arg = core::ArgObject::create<ArgTypeEnum::Selector>(selection);

    MockModelIOSystem io_sys;
    HandlerContext ctx { io_sys, comp_op };

    MeasureHandler handler;
    auto result_any = handler.execute(ctx, { type_arg, sel_arg });
    const std::string* result = std::any_cast<std::string>(&result_any);
    REQUIRE(result != nullptr);
    CHECK(result->find("累计长度: 2.000000") != std::string::npos);
}

TEST_CASE("MeasureHandler: angle between two edges selected as endpoint vertex id pairs")
{
    auto mesh = std::make_unique<MeshData>(MakeMeshData());
    auto c = std::make_unique<ComponentData>();
    c->id = -1;
    c->name = "Comp";
    c->mesh = std::move(mesh);

    ComponentDatas comps;
    comps.push_back(std::move(c));

    ModelLayer mgr;
    const Index model_id = mgr.addModel("test", std::move(comps));

    auto cids = mgr.modelById(model_id)->componentIds();
    REQUIRE(cids.size() == 1);

    ComponentData* comp = mgr.findComponent(cids[0]);
    REQUIRE(comp);
    ComponentOperator comp_op(cids[0], *comp, mgr, nullptr);

    MeshData* mesh_ptr = comp->asMeshData();
    REQUIRE(mesh_ptr);
    const Index base = mesh_ptr->local_to_global_[0];

    core::ArgObject type_arg = core::ArgObject::create<ArgTypeEnum::Combo>(1); // 角度
    // 边 {0,1} 方向 (1,0,0)，边 {0,3} 方向 (0,1,0)，夹角 90°
    auto selection = makeEdgeSelection({ base + 0, base + 1, base + 0, base + 3 });
    selection->component_id = cids[0];
    core::ArgObject sel_arg = core::ArgObject::create<ArgTypeEnum::Selector>(selection);

    MockModelIOSystem io_sys;
    HandlerContext ctx { io_sys, comp_op };

    MeasureHandler handler;
    auto result_any = handler.execute(ctx, { type_arg, sel_arg });
    const std::string* result = std::any_cast<std::string>(&result_any);
    REQUIRE(result != nullptr);
    CHECK(result->find("Angle: 90.000000 deg") != std::string::npos);
}

TEST_CASE("MeasureHandler: geometry edge length on OCC box")
{
    // 10×10×10 立方体任一条几何边长度均为 10
    const std::string result = executeGeometryMeasureOnBox(3, ElementEnum::GeometryEdge, 1);
    CHECK(result.find("累计长度: 10.000000") != std::string::npos);
}

TEST_CASE("MeasureHandler: geometry face area on OCC box")
{
    // 10×10×10 立方体任一个几何面面积均为 100
    const std::string result = executeGeometryMeasureOnBox(4, ElementEnum::GeometryFace, 1);
    CHECK(result.find("总面积: 100.000000") != std::string::npos);
}

TEST_CASE("MeasureHandler: geometry solid volume on OCC box")
{
    // 10×10×10 立方体体积为 1000
    const std::string result = executeGeometryMeasureOnBox(5, ElementEnum::GeometrySolid, 1);
    CHECK(result.find("总体积: 1000.000000") != std::string::npos);
}
