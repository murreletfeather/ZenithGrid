/**
 * @file TestMeshRepairPlugin.cpp
 * @brief MeshRepairHandler 的单元测试：补洞、自交检测、退化清理
 */

#include "MeshRepairHandler.h"
#include "ArgObject.h"
#include "ComponentData.h"
#include "EventBus.h"
#include "FeatureSystem.h"
#include "MeshData.h"
#include "ModelLayer.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

using namespace systems;
using namespace systems::feature;

namespace {

//! @brief 缺底面的四棱锥（底面四边形边界 = 1 个孔洞）
MeshData makeOpenPyramidMesh()
{
    MeshData mesh;
    mesh.vertex_positions_ = {
        { 0.5, 0.5, 1.0 },
        { 0.0, 0.0, 0.0 },
        { 1.0, 0.0, 0.0 },
        { 1.0, 1.0, 0.0 },
        { 0.0, 1.0, 0.0 },
    };
    mesh.face_vertices_ = { 0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1 };
    mesh.face_vertices_offset_ = { 0, 3, 6, 9, 12 };
    return mesh;
}

//! @brief 闭合四面体（无孔洞对照）
MeshData makeClosedTetMesh()
{
    MeshData mesh;
    mesh.vertex_positions_ = {
        { 0.0, 0.0, 0.0 },
        { 1.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 1.0 },
    };
    mesh.face_vertices_ = { 0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3 };
    mesh.face_vertices_offset_ = { 0, 3, 6, 9, 12 };
    return mesh;
}

//! @brief 两个空间交叉的三角形（B 竖直穿过 A 内部）
MeshData makeCrossingTrianglesMesh()
{
    MeshData mesh;
    mesh.vertex_positions_ = {
        { 0.0, 0.0, 0.0 },
        { 2.0, 0.0, 0.0 },
        { 1.0, 2.0, 0.0 },
        { 1.0, 0.5, -1.0 },
        { 1.0, 0.5, 1.0 },
        { 1.0, -1.0, 0.0 },
    };
    mesh.face_vertices_ = { 0, 1, 2, 3, 4, 5 };
    mesh.face_vertices_offset_ = { 0, 3, 6 };
    return mesh;
}

//! @brief 一个正常三角面 + 一个共线零面积退化面
MeshData makeDegenerateMesh()
{
    MeshData mesh;
    mesh.vertex_positions_ = {
        { 0.0, 0.0, 0.0 },
        { 1.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0 },
        { 2.0, 0.0, 0.0 },
        { 3.0, 0.0, 0.0 },
        { 4.0, 0.0, 0.0 },
    };
    mesh.face_vertices_ = { 0, 1, 2, 3, 4, 5 };
    mesh.face_vertices_offset_ = { 0, 3, 6 };
    return mesh;
}

struct RepairResult {
    std::string text;
    Index face_count; //> 写回后组件面数（未写回则为原面数）
};

HandlerMetaData repairMetaData()
{
    HandlerMetaData metadata;
    metadata.name = "MeshRepair";
    metadata.display_name = "网格修复";
    return metadata;
}

//! @brief 在单组件模型上通过 FeatureSystem 执行修复操作
RepairResult executeRepair(MeshData mesh_data, int op_index)
{
    auto mesh = std::make_unique<MeshData>(std::move(mesh_data));
    auto c = std::make_unique<ComponentData>();
    c->id = -1;
    c->name = "Comp";
    c->mesh = std::move(mesh);

    ComponentDatas comps;
    comps.push_back(std::move(c));

    ModelLayer mgr;
    const Index model_id = mgr.addModel("test", std::move(comps));
    const auto cids = mgr.modelById(model_id)->componentIds();
    const Index comp_id = cids[0];

    core::EventBus bus;
    FeatureSystem feature_system(mgr, bus);

    FeatureSystem::SystemHandlerPtr handler { new MeshRepairHandler };
    feature_system.registerHandler(repairMetaData(), std::move(handler));
    feature_system.setActiveComponentProvider(
        [comp_id]() { return std::optional<Index> { comp_id }; });

    feature_system.setParameter(
        "MeshRepair", 0, core::ArgObject::create<ArgTypeEnum::Combo>(op_index));

    const auto result_any = feature_system.invoke("MeshRepair");
    const auto* result = std::any_cast<std::string>(&result_any);

    ComponentData* comp = mgr.findComponent(comp_id);
    RepairResult r;
    r.text = result ? *result : std::string();
    r.face_count = comp && comp->mesh
        ? static_cast<Index>(comp->mesh->face_vertices_offset_.size()) - 1
        : -1;
    return r;
}

} // namespace

TEST_CASE("MeshRepair fills a hole in an open pyramid", "[MeshRepairPlugin]")
{
    const RepairResult r = executeRepair(makeOpenPyramidMesh(), 0);
    REQUIRE(r.text.find("已修补 1/1 个孔洞") != std::string::npos);
    REQUIRE(r.face_count == 6); // 4 侧面 + 底面四边形补成 2 个三角面
}

TEST_CASE("MeshRepair reports no holes for a closed mesh", "[MeshRepairPlugin]")
{
    const RepairResult r = executeRepair(makeClosedTetMesh(), 0);
    REQUIRE(r.text.find("无孔洞") != std::string::npos);
}

TEST_CASE("MeshRepair detects self intersections", "[MeshRepairPlugin]")
{
    const RepairResult r = executeRepair(makeCrossingTrianglesMesh(), 1);
    REQUIRE(r.text.find("检测到") != std::string::npos);
    REQUIRE(r.text.find("1 对") != std::string::npos);
}

TEST_CASE("MeshRepair removes degenerate faces", "[MeshRepairPlugin]")
{
    const RepairResult r = executeRepair(makeDegenerateMesh(), 2);
    REQUIRE(r.text.find("已移除 1 个退化面") != std::string::npos);
    REQUIRE(r.face_count == 1);
}
