/**
 * @file RidgesUmbilicsHandler.cpp
 * @brief 基于 CGAL Ridges_3 的脊线与脐点近似分析功能处理器实现
 * @date 2026-09-13
 */

// CGAL 头文件必须置于所有 OCC 相关头文件之前：
// OCC 的 Standard_Handle.hxx 将 Handle 定义为宏（Handle(X) -> opencascade::handle<X>），
// 若先引入 OCC，宏会污染后续解析的 CGAL/Handle.h 类声明，造成大片级联语法错误
#include <CGAL/Monge_via_jet_fitting.h>
#include <CGAL/PolyhedralSurf_neighbors.h>
#include <CGAL/Ridges.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Umbilics.h>
#include <CGAL/barycenter.h>
#include <CGAL/boost/graph/graph_traits_Surface_mesh.h>
#include <CGAL/boost/graph/properties_Surface_mesh.h>

#include "RidgesUmbilicsHandler.h"

#include "ArgObject.h"
#include "ArgType.h"
#include "CgalExceptionGuard.h"
#include "CgalMeshAdapter.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "FeatureContext.h"
#include "FeatureParams.h"
#include "FeatureRegistrar.h"
#include "GeometryBuilder.h"
#include "GeometryData.h"
#include "MeshData.h"
#include "ModelLayer.h"
#include "ModelOperator.h"
#include "Selection.h"

#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <TopoDS_Compound.hxx>

#include <boost/property_map/property_map.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace systems::feature {

namespace {

// 参数下标（与 setup() 注册顺序一一对应；同步见 README.md）
constexpr std::size_t kSelectorParam = 0; //> 目标 component 选择器（Component 类型选择）
constexpr std::size_t kRidgeKindParam = 1; //> 脊线类型（最大/最小/crest/全部）
constexpr std::size_t kRidgeOrderParam = 2; //> 脊线阶数（三阶/四阶）
constexpr std::size_t kRingCountParam = 3; //> 拟合邻域环数（0=自动）
constexpr std::size_t kUmbilicsParam = 4; //> 是否计算脐点

//! @brief 全曲面近似脐点判定比例：曲面 |k1-k2| 中位数与最大曲率均值之比低于
//! 该值时，视为处处脐点的退化曲面（完整球面最典型）。实测 32×24 UV 球面的
//! 中位数约 0.4%（各向异性邻域的拟合误差远大于双精度噪声），椭球等真实脊线
//! 曲面约 24%，阈值取二者中间
constexpr double kUmbilicEverywhereRatio = 2e-2;

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
 * addGeometryComponent 属结构操作，通知即时发出并随 FeatureSystem::invoke
 * 边界即时成 undo 记录，无需快照回滚。
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

void RidgesUmbilicsHandler::setup(FeatureRegistrar& reg, FeatureContext&)
{
    reg.addParameter({
        ArgTypeEnum::Selector,
        "目标 Component",
        "Component",
        "选择需要分析脊线与脐点的三角网格 Component",
    });
    reg.addParameter({
        ArgTypeEnum::Combo,
        "脊线类型",
        "最大,最小,crest,全部|3",
        "最大/最小为沿最大/最小主曲率方向的曲率极值线（各分椭圆/双曲两类），"
        "crest 为曲率沿主方向的极值线（需四阶）；默认全部。",
    });
    reg.addParameter({
        ArgTypeEnum::Combo,
        "脊线阶数",
        "三阶,四阶",
        "三阶用 b0/b3 标注椭圆/双曲脊线；四阶额外拟合 P1/P2 并支持 crest 脊线与 sharpness，耗时更高。",
    });
    reg.addParameter({
        ArgTypeEnum::Int,
        "拟合邻域环数（0=自动）",
        "0",
        "Monge 拟合的顶点邻域大小；自动模式按拟合阶数扩环至点数充足，网格稀疏或噪声较大时可手动增大。",
    });
    reg.addParameter({
        ArgTypeEnum::Combo,
        "是否计算脐点",
        "是,否|0",
        "脐点为两主曲率相等的点（如椭球面的 4 个椭圆脐点）。",
    });
    reg.addMenuItem({ "功能/网格", "脊线与脐点分析" });
}

std::any RidgesUmbilicsHandler::execute(FeatureContext& ctx)
{
    // 目标 Component 由 Selector 参数显式解析（AGENTS.md §10：不依赖对象树选中态）
    const auto* selection_ptr = ctx.params.value(kSelectorParam).get<ArgTypeEnum::Selector>();
    if (!selection_ptr || !*selection_ptr) {
        return std::string("请选择一个 Component");
    }
    const Selection& selection = **selection_ptr;
    if (selection.type != ElementEnum::Component || selection.ids.size() != 1) {
        return std::string("请选择且仅选择一个 Component");
    }
    const Index component_id = selection.ids.front();

    auto comp_op = ctx.componentOperator ? ctx.componentOperator(component_id) : std::nullopt;
    if (!comp_op || !comp_op->mesh()) {
        return std::string("当前 Component 没有表面网格，无法分析脊线与脐点");
    }
    const MeshData& input_mesh = *comp_op->mesh();

    // 入口预检：曲率脊线只对纯三角表面有意义。含体单元/非三角面时先一步拦截给出
    // 直白文案（MeshRepair 同款；toSurfaceMesh 对非三角面内部也会 throw）
    if (hasSolids(input_mesh)) {
        return std::string("脊线与脐点分析仅支持纯三角表面网格（当前 Component 含体单元，请改用其他工具）");
    }
    if (!isAllTriangular(input_mesh)) {
        return std::string("脊线与脐点分析仅支持纯三角表面网格（当前 Component 含非三角面，请先转换为全三角网格）");
    }

    const int* ridge_kind = ctx.params.value(kRidgeKindParam).get<ArgTypeEnum::Combo>();
    const int* ridge_order_idx = ctx.params.value(kRidgeOrderParam).get<ArgTypeEnum::Combo>();
    const long long* ring_count = ctx.params.value(kRingCountParam).get<ArgTypeEnum::Int>();
    const int* umbilics_enabled = ctx.params.value(kUmbilicsParam).get<ArgTypeEnum::Combo>();
    if (!ridge_kind || *ridge_kind < 0 || *ridge_kind > 3
        || !ridge_order_idx || *ridge_order_idx < 0 || *ridge_order_idx > 1
        || !ring_count || *ring_count < 0
        || !umbilics_enabled || *umbilics_enabled < 0) {
        return std::string("错误：脊线与脐点分析参数无效");
    }

    // 作用域内 CGAL 断言失败 → 抛 C++ 异常（详见 CgalExceptionGuard 注释）
    cgalsupport::CgalExceptionGuard cgal_guard;

    try {
        // 只读转换到 CGAL Surface_mesh（顶点索引与 MeshData 局部点 id 一致），原网格不动
        CgalMesh cgal_mesh = toSurfaceMesh(input_mesh);
        const auto vpm = get(CGAL::vertex_point, cgal_mesh);

        // ---- 1. 逐顶点 Monge jet 拟合主曲率/主方向与三、四阶系数 ----
        const std::size_t monge_degree = *ridge_order_idx == 0 ? 3 : 4;
        const std::size_t required_points = (monge_degree + 1) * (monge_degree + 2) / 2;

        VertexFTMap k1_map, k2_map, b0_map, b3_map, p1_map, p2_map;
        VertexVectorMap d1_map, d2_map;

        CGAL::T_PolyhedralSurf_neighbors<CgalMesh> neighbors(cgal_mesh);
        std::size_t degraded_count = 0;
        // 曲率量级统计（仅拟合成功的顶点参与）：用于识别处处脐点的退化曲面。
        // 个别顶点拟合噪声可能很大（如球面极区各向异性邻域），判据取全体中位数而非最大值
        std::vector<double> curvature_diffs;
        double max_curvature_scale = 0;
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
                const double k1 = monge_form.principal_curvatures(0);
                const double k2 = monge_form.principal_curvatures(1);
                k1_map[vertex] = k1;
                k2_map[vertex] = k2;
                curvature_diffs.push_back(std::abs(k1 - k2));
                max_curvature_scale = std::max(max_curvature_scale, 0.5 * (std::abs(k1) + std::abs(k2)));
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
            spdlog::warn("[RidgesUmbilics] {} 个顶点邻域不足/退化，已按平坦量处理", degraded_count);
        }

        // 处处脐点的退化曲面（完整球面最典型）：k1≈k2 仅剩拟合噪声，主方向无定义，
        // 脊线在数学上不存在、脐点处处都是，继续提取只会得到噪声结果，且其中的
        // 近重合退化线段会被 OCC 拒绝而中断整个分析，故直接给出明确结论
        if (!curvature_diffs.empty() && max_curvature_scale > 0) {
            const auto median_it = curvature_diffs.begin()
                + static_cast<std::ptrdiff_t>(curvature_diffs.size() / 2);
            std::nth_element(curvature_diffs.begin(), median_it, curvature_diffs.end());
            spdlog::info("[RidgesUmbilics] 曲率统计：|k1-k2| 中位数={:.3e}，曲率量级={:.3e}",
                *median_it, max_curvature_scale);
            if (*median_it < kUmbilicEverywhereRatio * max_curvature_scale) {
                return std::string("曲面所有顶点均近似为脐点（k1≈k2，如完整球面），不存在脊线，未生成结果组件");
            }
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
        auto model_operator = ctx.model.getModelOperator(comp_op->modelId());
        if (!model_operator) {
            spdlog::error("[RidgesUmbilics] 未找到 component {} 所属模型，无法写入结果组件",
                component_id);
            return std::string("脊线与脐点分析失败：未找到目标组件所属模型，无法写入结果组件");
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
            std::size_t edge_count = 0;
            for (const auto& polyline : polylines) {
                for (std::size_t i = 1; i < polyline.size(); ++i) {
                    const CgalPoint3& start = polyline[i - 1];
                    const CgalPoint3& end = polyline[i];
                    const double dx = end.x() - start.x();
                    const double dy = end.y() - start.y();
                    const double dz = end.z() - start.z();
                    // 按 OCC 容差过滤退化段：makeLine 对端点距离 ≤ Precision::Confusion
                    // 的线段抛错，不过滤会让单个退化段中断整个分析
                    if (dx * dx + dy * dy + dz * dz <= Precision::Confusion() * Precision::Confusion()) {
                        continue;
                    }
                    builder.Add(compound, GeometryBuilder::makeLine(start.x(), start.y(), start.z(), end.x(), end.y(), end.z()));
                    ++edge_count;
                }
            }

            // 全部线段被过滤时不建空组件
            if (edge_count == 0) {
                continue;
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
            return std::string("未检测到脊线/脐点，未生成结果组件");
        }
        std::ostringstream oss;
        oss << "脊线与脐点分析完成：脊线 " << total_lines << " 条、脐点 " << total_umbilics
            << " 个，已生成 " << added_components << " 个结果组件（对象树 Ridge_* / Umbilic_*）";
        return oss.str();
    } catch (const CGAL::Failure_exception& e) {
        // CGAL 内部几何不变量违反：细节留日志便于开发定位，UI 仅返回固定温和文案
        spdlog::error("[RidgesUmbilics] CGAL 断言失败: expr={}, file={}:{}",
            e.expression(), e.filename(), e.line_number());
        return std::string("脊线与脐点分析失败：当前网格不满足曲率拟合前提");
    } catch (const std::exception& e) {
        spdlog::error("[RidgesUmbilics] CGAL 操作异常: {}", e.what());
        return std::string("脊线与脐点分析失败：") + e.what();
    }
}

} // namespace systems::feature
