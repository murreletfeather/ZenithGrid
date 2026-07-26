/**
 * @file MeshRepairHandler.cpp
 * @brief 网格修复处理器：基于 CGAL PMP 的补洞、自交检测与退化清理
 */

// CGAL 头文件必须置于所有 OCC 相关头文件之前：
// OCC 的 Standard_Handle.hxx 将 Handle 定义为宏（Handle(X) -> opencascade::handle<X>），
// 若先引入 OCC，宏会污染后续解析的 CGAL/Handle.h 类声明，造成大片级联语法错误
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>

#include "MeshRepairHandler.h"
#include "ArgObject.h"
#include "CgalMeshAdapter.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "FeatureContext.h"
#include "FeatureParams.h"
#include "FeatureRegistrar.h"
#include "MeshData.h"
#include "ModelLayer.h"

#include <spdlog/spdlog.h>

#include <sstream>

namespace systems::feature {

namespace {

//! @brief 替换当前组件的网格并维护全局点索引与边 id 映射
//! 流程：释放旧边映射、追加入全局点池、转全局索引、重建边映射、通知 observer
bool replaceComponentMesh(ComponentOperator& comp, std::unique_ptr<MeshData> mesh)
{
    ModelLayer& mgr = comp.manager();
    const Index component_id = comp.componentId();
    ComponentData* component = mgr.findComponent(component_id);
    if (!component || !mesh)
        return false;

    if (component->mesh)
        component->mesh->releaseEdgeIdMap(mgr.edgeIdMap());

    const Index base = mgr.appendGlobalPoints(mesh->vertex_positions_);
    mesh->vertex_count_ = static_cast<Index>(mesh->vertex_positions_.size());
    mesh->local_to_global_.resize(mesh->vertex_count_);
    for (Index i = 0; i < mesh->vertex_count_; ++i)
        mesh->local_to_global_[static_cast<size_t>(i)] = base + i;
    mesh->makePointIdsGlobal();
    std::vector<std::array<double, 3>> {}.swap(mesh->vertex_positions_);
    mesh->ensureEdgeIdMapBuilt(mgr.edgeIdMap(), component_id);

    component->mesh = std::move(mesh);
    comp.notifyChanged();
    return true;
}

//! @brief 把 CGAL 网格写回当前组件（局部索引 MeshData → 入全局点池替换）
bool writeBack(ComponentOperator& comp, const CgalMesh& sm)
{
    auto out_mesh = std::make_unique<MeshData>();
    fromSurfaceMesh(sm, *out_mesh);
    return replaceComponentMesh(comp, std::move(out_mesh));
}

std::string fillHoles(ComponentOperator& comp, CgalMesh& sm)
{
    namespace PMP = CGAL::Polygon_mesh_processing;

    // 先收集全部边界环（孔洞），逐个三角化修补；填补只增不删，已收集的边界半边保持有效
    std::vector<CgalMesh::Halfedge_index> border_cycles;
    PMP::extract_boundary_cycles(sm, std::back_inserter(border_cycles));
    if (border_cycles.empty())
        return std::string("网格无孔洞（未发现边界环）");

    std::vector<CgalMesh::Face_index> new_faces;
    for (const auto h : border_cycles)
        PMP::triangulate_hole(sm, h, CGAL::parameters::face_output_iterator(std::back_inserter(new_faces)));

    if (new_faces.empty())
        return std::string("孔洞修补失败（边界可能为非流形，无法三角化）");

    if (!writeBack(comp, sm))
        return std::string("错误：修复结果写回组件失败");

    std::ostringstream oss;
    oss << "已修补 " << border_cycles.size() << " 个孔洞，新增 " << new_faces.size()
        << " 个三角面，网格已更新";
    return oss.str();
}

std::string detectSelfIntersections(const CgalMesh& sm)
{
    namespace PMP = CGAL::Polygon_mesh_processing;

    std::vector<std::pair<CgalMesh::Face_index, CgalMesh::Face_index>> intersecting;
    PMP::self_intersections(sm, std::back_inserter(intersecting));
    if (intersecting.empty())
        return std::string("未发现自相交面");

    std::ostringstream oss;
    oss << "检测到 " << intersecting.size() << " 对自相交面：\n";
    const std::size_t limit = std::min<std::size_t>(intersecting.size(), 10);
    for (std::size_t i = 0; i < limit; ++i)
        oss << "面 " << intersecting[i].first << " × 面 " << intersecting[i].second << "\n";
    if (intersecting.size() > limit)
        oss << "…… 共 " << intersecting.size() << " 对";
    return oss.str();
}

std::string removeDegenerateFaces(ComponentOperator& comp, CgalMesh& sm)
{
    namespace PMP = CGAL::Polygon_mesh_processing;

    const std::size_t before = sm.number_of_faces();
    PMP::remove_degenerate_faces(sm);
    const std::size_t removed = before - sm.number_of_faces();
    if (removed == 0)
        return std::string("未发现退化面");

    if (!writeBack(comp, sm))
        return std::string("错误：修复结果写回组件失败");

    std::ostringstream oss;
    oss << "已移除 " << removed << " 个退化面，网格已更新";
    return oss.str();
}

} // namespace

void MeshRepairHandler::setup(FeatureRegistrar& reg)
{
    reg.addParameter({
        ArgTypeEnum::Combo,
        "操作类型",
        "补洞,自交检测,退化清理|0",
        "选择要执行的网格修复操作",
    });
    reg.addMenuItem({ "功能/网格", "网格修复" });
}

std::any MeshRepairHandler::execute(FeatureContext& ctx)
{
    const auto component_id = ctx.activeComponent ? ctx.activeComponent() : std::nullopt;
    if (!component_id)
        return std::string("未选择 Component");
    auto comp_op = ctx.componentOperator ? ctx.componentOperator(*component_id) : std::nullopt;
    if (!comp_op || !comp_op->mesh())
        return std::string("当前 Component 没有网格数据（网格修复仅支持网格模型）");

    int op_index = 0;
    if (const auto* v = ctx.params.value(0).get<ArgTypeEnum::Combo>())
        op_index = *v;

    MeshData* mesh = comp_op->mesh();
    ModelLayer& manager = comp_op->manager();

    try {
        CgalMesh sm = toSurfaceMesh(*mesh, manager.globalPoints());
        switch (op_index) {
        case 0:
            return fillHoles(*comp_op, sm);
        case 1:
            return detectSelfIntersections(sm);
        case 2:
            return removeDegenerateFaces(*comp_op, sm);
        default:
            return std::string("错误：未知操作类型");
        }
    } catch (const std::exception& e) {
        spdlog::error("[MeshRepair] CGAL 操作异常: op_index={}, error={}", op_index, e.what());
        return std::string("错误：") + e.what();
    }
}

} // namespace systems::feature
