/**
 * @file RidgesUmbilicsHandler.cpp
 * @brief 基于 CGAL Ridges_3 的脊线与脐点近似分析处理器实现
 * @date 2026-09-13
 */
#include "RidgesUmbilicsHandler.h"

#include "ArgObject.h"
#include "CgalExceptionGuard.h"
#include "CgalMeshAdapter.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "GeometryBuilder.h"
#include "GeometryData.h"
#include "MeshData.h"
#include "ModelLayer.h"
#include "ModelOperator.h"
#include "Selection.h"

#include <CGAL/Monge_via_jet_fitting.h>
#include <CGAL/PolyhedralSurf_neighbors.h>
#include <CGAL/Ridges.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Umbilics.h>
#include <CGAL/barycenter.h>
#include <CGAL/boost/graph/graph_traits_Surface_mesh.h>
#include <CGAL/boost/graph/properties_Surface_mesh.h>

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <boost/property_map/property_map.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// 参数下标（与 args_type() 注册顺序一一对应；同步见 README.md）
constexpr std::size_t kSelectorParam = 0; //> 目标 component 选择器（Component 类型选择）
constexpr std::size_t kRidgeKindParam = 1; //> 脊线类型（最大/最小/crest/全部）
constexpr std::size_t kRidgeOrderParam = 2; //> 脊线阶数（三阶/四阶）
constexpr std::size_t kRingCountParam = 3; //> 拟合邻域环数（0=自动）
constexpr std::size_t kUmbilicsParam = 4; //> 是否计算脐点

//! @brief Monge jet 拟合的数据内核（取默认 Simple_cartesian<double>，SVD 由 Eigen 提供）
using FitKernel = CGAL::Simple_cartesian<double>;
using MongeFitting = CGAL::Monge_via_jet_fitting<FitKernel>;

//! @brief 顶点标量属性表（k1/k2/b0/b3/P1/P2），键为 CGAL 网格顶点
using VertexFTMap = std::map<CgalMesh::Vertex_index, CgalKernel::FT>;
//! @brief 顶点主方向属性表（d1/d2）
using VertexVectorMap = std::map<CgalMesh::Vertex_index, CgalKernel::Vector_3>;
//! @brief 属性表的 boost 关联容器属性映射包装（Ridge/Umbilic 近似类的模板参数形态）
using FTPropertyMap = boost::associative_property_map<VertexFTMap>;
using VectorPropertyMap = boost::associative_property_map<VertexVectorMap>;

//! @brief 判断 MeshData 是否含体单元（体单元网格不适用于曲率脊线分析）
bool hasSolids(const MeshData& mesh)
{
    return mesh.solid_vertices_offset_.size() > 1
        || mesh.solid_faces_offset_.size() > 1
        || mesh.solid_faces_vertices_offset_.size() > 1;
}

//! @brief 判断 MeshData 是否全三角面（任一非三角面即 false）
bool isAllTriangular(const MeshData& mesh)
{
    for (std::size_t i = 0; i + 1 < mesh.face_vertices_offset_.size(); ++i) {
        if (mesh.face_vertices_offset_[i + 1] - mesh.face_vertices_offset_[i] != 3)
            return false;
    }
    return true;
}

//! @brief Ridge_type → 结果组件名（对象树展示）
const char* ridgeComponentName(CGAL::Ridge_type type)
{
    switch (type) {
    case CGAL::MAX_ELLIPTIC_RIDGE:
        return "Ridge_MaxElliptic";
    case CGAL::MAX_HYPERBOLIC_RIDGE:
        return "Ridge_MaxHyperbolic";
    case CGAL::MAX_CREST_RIDGE:
        return "Ridge_MaxCrest";
    case CGAL::MIN_ELLIPTIC_RIDGE:
        return "Ridge_MinElliptic";
    case CGAL::MIN_HYPERBOLIC_RIDGE:
        return "Ridge_MinHyperbolic";
    case CGAL::MIN_CREST_RIDGE:
        return "Ridge_MinCrest";
    default:
        return "Ridge_Unknown";
    }
}

//! @brief Umbilic_type → 结果组件名（对象树展示）
const char* umbilicComponentName(CGAL::Umbilic_type type)
{
    switch (type) {
    case CGAL::ELLIPTIC_UMBILIC:
        return "Umbilic_Elliptic";
    case CGAL::HYPERBOLIC_UMBILIC:
        return "Umbilic_Hyperbolic";
    default:
        return "Umbilic_NonGeneric";
    }
}

/**
 * @brief 把结果形状包装成新几何组件并加入模型
 *
 * 结果按类型独立成组件，便于对象树按类显隐；调用方对空结果不建组件。
 * undo 随算法系统操作边界自动记录（结构操作即时成记录），无需快照回滚。
 * @return 新组件 id
 */
Index addResultComponent(ModelOperator& model_operator, const std::string& name, TopoDS_Shape shape)
{
    auto geometry = std::make_unique<GeometryData>();
    geometry->setRootShape(std::move(shape));
    auto component = std::make_unique<ComponentData>();
    component->name = name;
    component->geometry = std::move(geometry);
    return model_operator.addGeometryComponent(std::move(component));
}

} // namespace

std::optional<Index> systems::algo::RidgesUmbilicsHandler::resolveComponentId(
    ModelLayer& model_layer,
    Index fallback_component_id,
    const std::vector<core::ArgObject>& args) const
{
    (void)model_layer;

    // 两路径解析目标 component，遵循 AGENTS.md 第 10 节"算法系统不依赖对象树选中态"：
    //   路径 A（首选）：选择器直接选定 Component 类型（单选）
    //   路径 B（兜底）：fallback_component_id（对象树选中态，仅供兼容保留）
    if (args.size() > kSelectorParam) {
        const auto* selection_ptr = args[kSelectorParam].get<ArgTypeEnum::Selector>();
        if (selection_ptr && *selection_ptr) {
            const auto& sel = **selection_ptr;

            if (sel.type == ElementEnum::Component && !sel.ids.empty()) {
                if (sel.ids.size() == 1) {
                    return sel.ids.front();
                }
                spdlog::error(
                    "RidgesUmbilicsHandler: 只能选择一个 component（当前选中 {} 个）",
                    sel.ids.size());
                return std::nullopt;
            }

            spdlog::warn(
                "RidgesUmbilicsHandler: Selector 内容不匹配（type={}），期望 Component 类型选择",
                static_cast<int>(sel.type));
        }
    }

    if (fallback_component_id < 0) {
        spdlog::error(
            "RidgesUmbilicsHandler: 解析目标 component 失败：请通过选择器（组件模式）或在对象树中选中目标组件。");
        return std::nullopt;
    }
    spdlog::info(
        "RidgesUmbilicsHandler: 选择器未提供目标 component，回退到对象树选中态 component_id={}",
        fallback_component_id);
    return fallback_component_id;
}

std::any systems::algo::RidgesUmbilicsHandler::execute(
    HandlerContext& context, const std::vector<core::ArgObject>& args)
{
    if (args.size() <= kUmbilicsParam) {
        spdlog::error("RidgesUmbilicsHandler: 参数数量不足（{} 个）", args.size());
        return {};
    }
    const int* ridge_kind = args[kRidgeKindParam].get<ArgTypeEnum::Combo>();
    const int* ridge_order_idx = args[kRidgeOrderParam].get<ArgTypeEnum::Combo>();
    const long long* ring_count = args[kRingCountParam].get<ArgTypeEnum::Int>();
    const int* umbilics_enabled = args[kUmbilicsParam].get<ArgTypeEnum::Combo>();
    if (!ridge_kind || *ridge_kind < 0 || *ridge_kind > 3
        || !ridge_order_idx || *ridge_order_idx < 0 || *ridge_order_idx > 1
        || !ring_count || *ring_count < 0
        || !umbilics_enabled || *umbilics_enabled < 0) {
        spdlog::error("RidgesUmbilicsHandler: 参数无效");
        return {};
    }

    // AlgorithmSystem 已按选择器解析出的 component_id 组装 context.cur_component
    const ComponentData& input_component = context.cur_component.component();
    const MeshData* input_mesh = input_component.mesh.get();
    if (!input_mesh || input_mesh->face_vertices_offset_.size() <= 1) {
        spdlog::error("RidgesUmbilicsHandler: 当前 component {} 无表面网格，无法分析脊线与脐点。",
            context.cur_component.componentId());
        return {};
    }

    // 入口预检：曲率脊线只对纯三角表面有意义。含体单元/非三角面时先一步拦截给出
    // 直白文案（MeshRepair 同款；toSurfaceMesh 对非三角面内部也会 throw）
    if (hasSolids(*input_mesh)) {
        spdlog::error("RidgesUmbilicsHandler: 当前 component {} 含体单元，仅支持纯三角表面网格。",
            context.cur_component.componentId());
        return {};
    }
    if (!isAllTriangular(*input_mesh)) {
        spdlog::error("RidgesUmbilicsHandler: 当前 component {} 含非三角面，请先转换为全三角网格。",
            context.cur_component.componentId());
        return {};
    }

    // 作用域内 CGAL 断言失败 → 抛 C++ 异常（详见 CgalExceptionGuard 注释）
    cgalsupport::CgalExceptionGuard cgal_guard;

    try {
        // 只读转换到 CGAL Surface_mesh（顶点索引与 MeshData 局部点 id 一致），原网格不动
        CgalMesh cgal_mesh = toSurfaceMesh(*input_mesh);
        const auto vpm = get(CGAL::vertex_point, cgal_mesh);

        // ---- 1. 逐顶点 Monge jet 拟合主曲率/主方向与三、四阶系数 ----
        const std::size_t monge_degree = *ridge_order_idx == 0 ? 3 : 4;
        const std::size_t required_points = (monge_degree + 1) * (monge_degree + 2) / 2;

        VertexFTMap k1_map, k2_map, b0_map, b3_map, p1_map, p2_map;
        VertexVectorMap d1_map, d2_map;

        CGAL::T_PolyhedralSurf_neighbors<CgalMesh> neighbors(cgal_mesh);
        std::size_t degraded_count = 0;
        for (const CgalMesh::Vertex_index vertex : vertices(cgal_mesh)) {
            std::vector<CgalMesh::Vertex_index> vertex_neigh;
            std::list<CgalMesh::Halfedge_index> contour;
            CgalKernel::FT one_ring_size = 0;
            if (*ring_count > 0) {
                // compute_neighbors 内部先取一环，再按 size*OneRingSize 半径扩环
                neighbors.compute_neighbors(vertex, vertex_neigh, contour,
                    static_cast<CgalKernel::FT>(*ring_count));
            } else {
                // 自动模式：从一环起逐步扩环直至点数满足拟合阶要求
                neighbors.compute_one_ring(vertex, vertex_neigh, contour, one_ring_size);
                CgalKernel::FT rings = 1;
                while (vertex_neigh.size() < required_points && rings < 10) {
                    ++rings;
                    neighbors.compute_neighbors(vertex, vertex_neigh, contour, rings);
                }
            }

            std::vector<FitKernel::Point_3> fit_points;
            fit_points.reserve(vertex_neigh.size());
            for (const CgalMesh::Vertex_index neighbor : vertex_neigh) {
                const CgalPoint3& p = get(vpm, neighbor);
                fit_points.emplace_back(p.x(), p.y(), p.z());
            }

            // 邻域点数不足拟合阶要求（网格过稀疏/边界退化）时置平坦量跳过，
            // k1≠k2 避免退化顶点被误判为脐点
            if (fit_points.size() < required_points) {
                k1_map[vertex] = 0;
                k2_map[vertex] = 1;
                b0_map[vertex] = 0;
                b3_map[vertex] = 0;
                p1_map[vertex] = 0;
                p2_map[vertex] = 0;
                d1_map[vertex] = CgalKernel::Vector_3(1, 0, 0);
                d2_map[vertex] = CgalKernel::Vector_3(0, 1, 0);
                ++degraded_count;
                continue;
            }

            try {
                MongeFitting monge_fit;
                const MongeFitting::Monge_form monge_form = monge_fit(fit_points.begin(), fit_points.end(), monge_degree, monge_degree);
                k1_map[vertex] = monge_form.principal_curvatures(0);
                k2_map[vertex] = monge_form.principal_curvatures(1);
                const auto& max_dir = monge_form.maximal_principal_direction();
                const auto& min_dir = monge_form.minimal_principal_direction();
                d1_map[vertex] = CgalKernel::Vector_3(max_dir.x(), max_dir.y(), max_dir.z());
                d2_map[vertex] = CgalKernel::Vector_3(min_dir.x(), min_dir.y(), min_dir.z());
                b0_map[vertex] = monge_form.third_order_coefficients(0);
                b3_map[vertex] = monge_form.third_order_coefficients(3);
                if (monge_degree >= 4) {
                    // P1/P2 为 Monge 形式四阶极值系数 c0/c4
                    p1_map[vertex] = monge_form.fourth_order_coefficients(0);
                    p2_map[vertex] = monge_form.fourth_order_coefficients(4);
                } else {
                    // 三阶标注不用 P1/P2（sharpness 亦无效），置零占位
                    p1_map[vertex] = 0;
                    p2_map[vertex] = 0;
                }
            } catch (const std::exception&) {
                // 个别退化邻域（共线/重合点等）拟合失败不影响整体分析，同平坦量退化处理
                k1_map[vertex] = 0;
                k2_map[vertex] = 1;
                b0_map[vertex] = 0;
                b3_map[vertex] = 0;
                p1_map[vertex] = 0;
                p2_map[vertex] = 0;
                d1_map[vertex] = CgalKernel::Vector_3(1, 0, 0);
                d2_map[vertex] = CgalKernel::Vector_3(0, 1, 0);
                ++degraded_count;
            }
        }
        if (degraded_count > 0) {
            spdlog::warn("RidgesUmbilicsHandler: {} 个顶点邻域不足/退化，已按平坦量处理",
                degraded_count);
        }

        // ---- 2. 脊线提取（近似类持属性映射引用，包装对象须活过 compute 调用）----
        FTPropertyMap pm_k1(k1_map);
        FTPropertyMap pm_k2(k2_map);
        FTPropertyMap pm_b0(b0_map);
        FTPropertyMap pm_b3(b3_map);
        VectorPropertyMap pm_d1(d1_map);
        VectorPropertyMap pm_d2(d2_map);
        FTPropertyMap pm_p1(p1_map);
        FTPropertyMap pm_p2(p2_map);

        using RidgeApproximation = CGAL::Ridge_approximation<CgalMesh, FTPropertyMap, VectorPropertyMap>;
        RidgeApproximation ridge_approximation(
            cgal_mesh, pm_k1, pm_k2, pm_b0, pm_b3, pm_d1, pm_d2, pm_p1, pm_p2);

        const CGAL::Ridge_order ridge_order_tag = *ridge_order_idx == 0 ? CGAL::Ridge_order_3 : CGAL::Ridge_order_4;
        std::vector<CGAL::Ridge_line<CgalMesh>*> ridge_lines;
        if (*ridge_kind == 0 || *ridge_kind == 3) {
            ridge_approximation.compute_max_ridges(std::back_inserter(ridge_lines), ridge_order_tag);
        }
        if (*ridge_kind == 1 || *ridge_kind == 3) {
            ridge_approximation.compute_min_ridges(std::back_inserter(ridge_lines), ridge_order_tag);
        }
        if (*ridge_kind == 2 || *ridge_kind == 3) {
            ridge_approximation.compute_crest_ridges(std::back_inserter(ridge_lines), ridge_order_tag);
        }

        // Ridge_line 由 CGAL 堆上分配，转折线点列后释放；
        // 交点坐标与官方 dump_4ogl 一致：he 为 p→q 方向，r = barycenter(p, λ, q)
        std::map<CGAL::Ridge_type, std::vector<std::vector<CgalPoint3>>> ridge_polylines;
        for (const CGAL::Ridge_line<CgalMesh>* ridge_line : ridge_lines) {
            std::vector<CgalPoint3> polyline;
            polyline.reserve(ridge_line->line()->size());
            for (const auto& ridge_halfedge : *ridge_line->line()) {
                const CgalPoint3& p = get(vpm, target(opposite(ridge_halfedge.first, cgal_mesh), cgal_mesh));
                const CgalPoint3& q = get(vpm, target(ridge_halfedge.first, cgal_mesh));
                polyline.push_back(CGAL::barycenter(p, ridge_halfedge.second, q));
            }
            if (polyline.size() >= 2) {
                ridge_polylines[ridge_line->line_type()].push_back(std::move(polyline));
            }
        }
        std::for_each(ridge_lines.begin(), ridge_lines.end(),
            [](const CGAL::Ridge_line<CgalMesh>* line) { delete line; });

        // ---- 3. 脐点提取（k1-k2 邻域极小 + 方向偏离指数非零；邻域尺寸 1 环）----
        std::map<CGAL::Umbilic_type, std::vector<CgalPoint3>> umbilic_points;
        if (*umbilics_enabled == 0) {
            using UmbilicApproximation = CGAL::Umbilic_approximation<CgalMesh, FTPropertyMap, VectorPropertyMap>;
            UmbilicApproximation umbilic_approximation(cgal_mesh, pm_k1, pm_k2, pm_d1, pm_d2);
            std::vector<CGAL::Umbilic<CgalMesh>*> umbilics;
            umbilic_approximation.compute(std::back_inserter(umbilics), 1.0);
            for (const CGAL::Umbilic<CgalMesh>* umbilic : umbilics) {
                umbilic_points[umbilic->umbilic_type()].push_back(get(vpm, umbilic->vertex()));
                delete umbilic;
            }
        }

        // ---- 4. 结果按类型落成新几何组件（空类别不建组件）----
        ModelLayer& model_layer = context.cur_component.manager();
        auto model_operator = model_layer.getModelOperator(context.cur_component.modelId());
        if (!model_operator) {
            spdlog::error("RidgesUmbilicsHandler: 未找到 component {} 所属模型，无法写入结果组件。",
                context.cur_component.componentId());
            return {};
        }

        std::size_t total_lines = 0;
        std::size_t added_components = 0;
        for (auto& [ridge_type, polylines] : ridge_polylines) {
            if (polylines.empty()) {
                continue;
            }

            TopoDS_Compound compound;
            BRep_Builder builder;
            builder.MakeCompound(compound);
            for (const auto& polyline : polylines) {
                for (std::size_t i = 1; i < polyline.size(); ++i) {
                    const CgalPoint3& start = polyline[i - 1];
                    const CgalPoint3& end = polyline[i];
                    const double dx = end.x() - start.x();
                    const double dy = end.y() - start.y();
                    const double dz = end.z() - start.z();
                    // 过滤零长段，避免 OCC 对重合端点抛错
                    if (dx * dx + dy * dy + dz * dz < 1e-24) {
                        continue;
                    }
                    builder.Add(compound, GeometryBuilder::makeLine(start.x(), start.y(), start.z(), end.x(), end.y(), end.z()));
                }
            }

            addResultComponent(*model_operator, ridgeComponentName(ridge_type), std::move(compound));
            ++added_components;
            total_lines += polylines.size();
        }

        std::size_t total_umbilics = 0;
        for (auto& [umbilic_type, points] : umbilic_points) {
            if (points.empty()) {
                continue;
            }

            TopoDS_Compound compound;
            BRep_Builder builder;
            builder.MakeCompound(compound);
            for (const CgalPoint3& p : points) {
                builder.Add(compound, GeometryBuilder::makePoint(p.x(), p.y(), p.z()));
            }

            addResultComponent(*model_operator, umbilicComponentName(umbilic_type), std::move(compound));
            ++added_components;
            total_umbilics += points.size();
        }

        if (added_components == 0) {
            spdlog::warn("RidgesUmbilicsHandler: 组件 {} 未检测到脊线/脐点，未生成结果组件。",
                context.cur_component.componentId());
        } else {
            spdlog::info("RidgesUmbilicsHandler: 组件 {} 分析完成：{} 条脊线、{} 个脐点，新增 {} 个结果组件。",
                context.cur_component.componentId(), total_lines, total_umbilics, added_components);
        }
    } catch (const std::exception& error) {
        spdlog::error("RidgesUmbilicsHandler: 脊线与脐点分析失败: {}", error.what());
        return {};
    }
    return {};
}

std::vector<core::ArgType> systems::algo::RidgesUmbilicsHandler::args_type() const
{
    return {
        core::ArgType { ArgTypeEnum::Selector, "目标 Component", "Component",
            "选择需要分析脊线与脐点的三角网格 Component" },
        core::ArgType { ArgTypeEnum::Combo, "脊线类型", "最大,最小,crest,全部|3",
            "最大/最小为沿最大/最小主曲率方向的曲率极值线（各分椭圆/双曲两类），"
            "crest 为曲率沿主方向的极值线（需四阶）；默认全部。" },
        core::ArgType { ArgTypeEnum::Combo, "脊线阶数", "三阶,四阶",
            "三阶用 b0/b3 标注椭圆/双曲脊线；四阶额外拟合 P1/P2 并支持 crest 脊线与 sharpness，耗时更高。" },
        core::ArgType { ArgTypeEnum::Int, "拟合邻域环数（0=自动）", "0",
            "Monge 拟合的顶点邻域大小；自动模式按拟合阶数扩环至点数充足，网格稀疏或噪声较大时可手动增大。" },
        core::ArgType { ArgTypeEnum::Combo, "是否计算脐点", "是,否",
            "脐点为两主曲率相等的点（如椭球面的 4 个椭圆脐点）。" },
    };
}
