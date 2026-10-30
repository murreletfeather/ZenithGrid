#include "GeometryPointOps.h"
#include "GeometryRepairValidation.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Extrema_ExtPC.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dInt_GInter.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Vec2d.hxx>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
//! @brief 预览折线的相对弦偏差、最大递归深度及初始分段数，不改变提交几何的精度。
constexpr double kPathChordDeviationFactor = 1.0e-4;
constexpr int kPathMaximumSubdivisionDepth = 10;
constexpr int kPathInitialSubdivisions = 16;

/** @brief 保留 OCC 查询失败原因；每个调用线程每秒最多输出一条 debug，普通无命中不记日志。 */
void logQueryFailure(const char* operation, Index component_id, Index support_id, const Standard_Failure& error)
{
    auto* logger = spdlog::default_logger_raw();
    if (!logger || !logger->should_log(spdlog::level::debug))
        return;
    thread_local std::chrono::steady_clock::time_point next_report;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_report)
        return;
    next_report = now + std::chrono::seconds(1);
    const char* message = error.GetMessageString();
    logger->debug("GeometryPointOps: {} component={} support={}: {}", operation,
        component_id, support_id, message ? message : "Unknown OCC exception");
}

/** @brief 拒绝非有限坐标，避免无效鼠标输入进入 OCC 数值算法。 */
bool isFinitePoint(const std::array<double, 3>& point)
{
    return std::all_of(point.begin(), point.end(), [](double value) { return std::isfinite(value); });
}

/** @brief 组装支撑面参数与世界坐标，保留目标面身份。 */
GeometryPoint makeFacePoint(Index component_id, Index face_id, const gp_Pnt& point, double u, double v)
{
    GeometryPoint result;
    result.component_id = component_id;
    result.support_kind = GeometryPointKind::Face;
    result.support_id = face_id;
    result.world_pos = { point.X(), point.Y(), point.Z() };
    result.face_uv = { u, v };
    result.valid = true;
    return result;
}

/** @brief 把周期坐标移回当前面的参数边界范围，供修剪域分类使用。 */
double wrapParameter(double value, double first, double last, bool periodic, double period)
{
    if (!periodic || !std::isfinite(first) || !std::isfinite(last))
        return value;
    if (value >= first - Precision::PConfusion() && value <= last + Precision::PConfusion())
        return value;
    const double middle = (first + last) * 0.5;
    return value + std::round((middle - value) / period) * period;
}

/** @brief 修剪域分类器与周期参数边界共用的只读查询数据。 */
struct SurfaceDomain {
    TopoDS_Face face;
    occ::handle<Geom_Surface> surface;
    double first_u { }, last_u { }, first_v { }, last_v { };
    double tolerance { }, uv_tolerance { };

    /** @brief 捕获局部精度与面边界，不放宽无关模型的数值精度。 */
    explicit SurfaceDomain(const TopoDS_Face& source)
        : face(source)
        , surface(BRep_Tool::Surface(source))
    {
        if (source.IsNull() || surface.IsNull())
            throw std::invalid_argument("Target face has no surface");
        BRepTools::UVBounds(face, first_u, last_u, first_v, last_v);
        tolerance = std::max(BRep_Tool::Tolerance(face),
            geometry::repair::PrecisionPolicy::fromShape(face).classification);
        BRepAdaptor_Surface adaptor(face);
        uv_tolerance = std::max(Precision::PConfusion(),
            std::min(adaptor.UResolution(tolerance), adaptor.VResolution(tolerance)));
    }

    /** @brief 把自然曲面分支的 UV 归回实际面的参数域，保留已有缝边分支。 */
    gp_Pnt2d normalize(const gp_Pnt2d& point) const
    {
        const double u = wrapParameter(point.X(), first_u, last_u,
            surface->IsUPeriodic(), surface->IsUPeriodic() ? surface->UPeriod() : 0.0);
        const double v = wrapParameter(point.Y(), first_v, last_v,
            surface->IsVPeriodic(), surface->IsVPeriodic() ? surface->VPeriod() : 0.0);
        return gp_Pnt2d(u, v);
    }

    /** @brief 在原面的修剪域上分类连续展开后的 UV，支持跨越周期缝。 */
    TopAbs_State classify(const gp_Pnt2d& point) const
    {
        return BRepClass_FaceClassifier(face, normalize(point), tolerance).State();
    }
};

/**
 * @brief 按与所有边界 pcurve 的交点划分 UV 线段，逐区间检查修剪域。
 * @note 相交事件后的区间分类能发现很窄的孔洞，不能仅靠固定采样判定。
 */
void validateDomainSegment(const SurfaceDomain& domain, const gp_Pnt2d& first, const gp_Pnt2d& last)
{
    const gp_Vec2d delta(first, last);
    if (delta.SquareMagnitude() <= domain.uv_tolerance * domain.uv_tolerance)
        throw std::invalid_argument("Surface path contains repeated points");
    const occ::handle<Geom2d_TrimmedCurve> segment = GCE2d_MakeSegment(first, last).Value();
    std::vector<double> cuts { 0.0, 1.0 };
    const gp_Pnt2d middle((first.X() + last.X()) * 0.5, (first.Y() + last.Y()) * 0.5);
    // 周期面把边界复制到本段附近；缝边两侧都作为区间事件保留。
    for (TopExp_Explorer explorer(domain.face, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        double begin = 0.0, end = 0.0;
        const auto boundary = BRep_Tool::CurveOnSurface(TopoDS::Edge(explorer.Current()), domain.face, begin, end);
        if (boundary.IsNull() || !std::isfinite(begin) || !std::isfinite(end) || begin >= end)
            continue;
        const auto boundary_middle = boundary->Value((begin + end) * 0.5);
        const bool periodic_u = domain.surface->IsUPeriodic();
        const bool periodic_v = domain.surface->IsVPeriodic();
        const double period_u = periodic_u ? domain.surface->UPeriod() : 0.0;
        const double period_v = periodic_v ? domain.surface->VPeriod() : 0.0;
        const double offset_u = periodic_u ? std::round((middle.X() - boundary_middle.X()) / period_u) * period_u : 0.0;
        const double offset_v = periodic_v ? std::round((middle.Y() - boundary_middle.Y()) / period_v) * period_v : 0.0;
        for (int du = periodic_u ? -1 : 0; du <= (periodic_u ? 1 : 0); ++du) {
            for (int dv = periodic_v ? -1 : 0; dv <= (periodic_v ? 1 : 0); ++dv) {
                auto shifted = occ::handle<Geom2d_Curve>::DownCast(boundary->Copy());
                shifted->Translate(gp_Vec2d(offset_u + du * period_u, offset_v + dv * period_v));
                const occ::handle<Geom2d_TrimmedCurve> trim = new Geom2d_TrimmedCurve(shifted, begin, end);
                const Geom2dAPI_InterCurveCurve intersection(segment, trim, domain.uv_tolerance);
                if (!intersection.Intersector().IsDone())
                    throw std::runtime_error("Cannot verify surface path against the face boundary");
                for (int index = 1; index <= intersection.NbPoints(); ++index) {
                    const gp_Vec2d offset(first, intersection.Point(index));
                    cuts.push_back(std::clamp(offset.Dot(delta) / delta.SquareMagnitude(), 0.0, 1.0));
                }
                for (int index = 1; index <= intersection.NbSegments(); ++index) {
                    occ::handle<Geom2d_Curve> overlap_first, overlap_second;
                    intersection.Segment(index, overlap_first, overlap_second);
                    for (double parameter : { overlap_first->FirstParameter(), overlap_first->LastParameter() }) {
                        const gp_Vec2d offset(first, overlap_first->Value(parameter));
                        cuts.push_back(std::clamp(offset.Dot(delta) / delta.SquareMagnitude(), 0.0, 1.0));
                    }
                }
            }
        }
    }
    std::sort(cuts.begin(), cuts.end());
    // 各区间中点和端点都必须位于真实修剪面内；支撑曲面上的孔洞不是有效输入。
    for (std::size_t index = 1; index < cuts.size(); ++index) {
        if (cuts[index] - cuts[index - 1] <= Precision::PConfusion())
            continue;
        const double fraction = (cuts[index] + cuts[index - 1]) * 0.5;
        const auto state = domain.classify(gp_Pnt2d(first.X() + delta.X() * fraction, first.Y() + delta.Y() * fraction));
        if (state != TopAbs_IN && state != TopAbs_ON)
            throw std::invalid_argument("Surface path leaves the trimmed face or crosses a hole");
    }
}
}

std::optional<GeometryPoint> GeometryPointOps::intersectFace(
    const TopoDS_Face& face, Index component_id, Index face_id,
    const std::array<double, 3>& origin, const std::array<double, 3>& direction)
{
    if (face.IsNull() || !isFinitePoint(origin) || !isFinitePoint(direction))
        return std::nullopt;
    try {
        const gp_Vec vector(direction[0], direction[1], direction[2]);
        if (vector.SquareMagnitude() <= std::numeric_limits<double>::epsilon())
            return std::nullopt;
        IntCurvesFace_ShapeIntersector intersection;
        intersection.Load(face, std::max(BRep_Tool::Tolerance(face), Precision::Confusion()));
        intersection.PerformNearest(gp_Lin(gp_Pnt(origin[0], origin[1], origin[2]), gp_Dir(vector)),
            0.0, std::numeric_limits<double>::max());
        if (!intersection.IsDone() || intersection.NbPnt() == 0)
            return std::nullopt;
        int nearest = 1;
        for (int index = 2; index <= intersection.NbPnt(); ++index) {
            if (intersection.WParameter(index) < intersection.WParameter(nearest))
                nearest = index;
        }
        const SurfaceDomain domain(face);
        const auto uv = domain.normalize(gp_Pnt2d(intersection.UParameter(nearest), intersection.VParameter(nearest)));
        return makeFacePoint(component_id, face_id, intersection.Pnt(nearest), uv.X(), uv.Y());
    } catch (const Standard_Failure& error) {
        logQueryFailure("intersectFace", component_id, face_id, error);
        return std::nullopt;
    }
}

std::optional<GeometryPoint> GeometryPointOps::projectOnFace(
    const TopoDS_Face& face, Index component_id, Index face_id, const std::array<double, 3>& world_pos)
{
    if (face.IsNull() || !isFinitePoint(world_pos))
        return std::nullopt;
    try {
        const SurfaceDomain domain(face);
        const gp_Pnt input(world_pos[0], world_pos[1], world_pos[2]);
        const GeomAPI_ProjectPointOnSurf projection(input, domain.surface);
        std::optional<GeometryPoint> best;
        double distance = std::numeric_limits<double>::max();
        // 非平面可能有多个投影；只从位于修剪域的候选中选择最近者。
        for (int index = 1; index <= projection.NbPoints(); ++index) {
            double u = 0.0, v = 0.0;
            projection.Parameters(index, u, v);
            const auto state = domain.classify(gp_Pnt2d(u, v));
            if ((state == TopAbs_IN || state == TopAbs_ON) && projection.Distance(index) < distance) {
                distance = projection.Distance(index);
                const auto uv = domain.normalize(gp_Pnt2d(u, v));
                best = makeFacePoint(component_id, face_id, projection.Point(index), uv.X(), uv.Y());
            }
        }
        return best;
    } catch (const Standard_Failure& error) {
        logQueryFailure("projectOnFace", component_id, face_id, error);
        return std::nullopt;
    }
}

std::optional<GeometryPoint> GeometryPointOps::projectOnEdge(
    const TopoDS_Edge& edge, Index component_id, Index edge_id, const std::array<double, 3>& world_pos)
{
    if (edge.IsNull() || !isFinitePoint(world_pos))
        return std::nullopt;
    try {
        double first = 0.0, last = 0.0;
        const auto curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull() || !std::isfinite(first) || !std::isfinite(last) || first >= last)
            return std::nullopt;
        const gp_Pnt input(world_pos[0], world_pos[1], world_pos[2]);
        double parameter = first;
        double distance = input.SquareDistance(curve->Value(first));
        const double end_distance = input.SquareDistance(curve->Value(last));
        if (end_distance < distance) {
            parameter = last;
            distance = end_distance;
        }
        const GeomAPI_ProjectPointOnCurve projection(input, curve, first, last);
        for (int index = 1; index <= projection.NbPoints(); ++index) {
            if (projection.Point(index).SquareDistance(input) < distance) {
                parameter = projection.Parameter(index);
                distance = projection.Point(index).SquareDistance(input);
            }
        }
        const auto position = curve->Value(parameter);
        GeometryPoint result;
        result.component_id = component_id;
        result.support_kind = GeometryPointKind::Edge;
        result.support_id = edge_id;
        result.world_pos = { position.X(), position.Y(), position.Z() };
        result.edge_parameter = parameter;
        result.valid = true;
        return result;
    } catch (const Standard_Failure& error) {
        logQueryFailure("projectOnEdge", component_id, edge_id, error);
        return std::nullopt;
    }
}

std::optional<GeometryPoint> GeometryPointOps::projectEdgePointOnFace(
    const TopoDS_Face& face, const TopoDS_Edge& edge, Index component_id,
    Index face_id, Index edge_id, double edge_parameter)
{
    if (face.IsNull() || edge.IsNull() || !std::isfinite(edge_parameter))
        return std::nullopt;
    try {
        bool belongs_to_face = false;
        for (TopExp_Explorer boundary(face, TopAbs_EDGE); boundary.More(); boundary.Next())
            belongs_to_face = belongs_to_face || boundary.Current().IsSame(edge);
        if (!belongs_to_face)
            return std::nullopt;
        double first = 0.0, last = 0.0;
        const auto curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull() || !std::isfinite(first) || !std::isfinite(last) || first >= last
            || edge_parameter < first - Precision::PConfusion() || edge_parameter > last + Precision::PConfusion())
            return std::nullopt;
        const double parameter = std::clamp(edge_parameter, first, last);
        const gp_Pnt source = curve->Value(parameter);
        const SurfaceDomain domain(face);
        const double tolerance = std::max(Precision::Confusion(), BRep_Tool::Tolerance(edge));
        // 周期缝边在同一面上可能有两条 pcurve，各自保持原来的参数分支。
        const bool seam = BRep_Tool::IsClosed(edge, face);
        for (int branch = 0; branch < (seam ? 2 : 1); ++branch) {
            auto boundary = edge;
            if (branch != 0)
                boundary.Reverse();
            double pc_first = 0.0, pc_last = 0.0;
            const auto pcurve = BRep_Tool::CurveOnSurface(boundary, face, pc_first, pc_last);
            if (pcurve.IsNull() || !std::isfinite(pc_first) || !std::isfinite(pc_last) || pc_first >= pc_last)
                continue;
            const bool same_parameter = BRep_Tool::SameParameter(edge);
            if (same_parameter && (std::abs(pc_first - first) > Precision::PConfusion() || std::abs(pc_last - last) > Precision::PConfusion()))
                continue;
            double pc_parameter = parameter;
            if (!same_parameter) {
                // 不同参数化的对应必须在这条 pcurve 的三维嵌入上求解，不能线性换算或投到整张面。
                BRepAdaptor_Curve boundary_curve(boundary, face);
                pc_parameter = pc_first;
                double distance = boundary_curve.Value(pc_first).SquareDistance(source);
                const double end_distance = boundary_curve.Value(pc_last).SquareDistance(source);
                if (end_distance < distance) {
                    pc_parameter = pc_last;
                    distance = end_distance;
                }
                const Extrema_ExtPC projection(source, boundary_curve, pc_first, pc_last);
                if (!projection.IsDone())
                    continue;
                for (int index = 1; index <= projection.NbExt(); ++index) {
                    if (projection.SquareDistance(index) < distance) {
                        pc_parameter = projection.Point(index).Parameter();
                        distance = projection.SquareDistance(index);
                    }
                }
            }
            const auto uv = domain.normalize(pcurve->Value(pc_parameter));
            const auto position = domain.surface->Value(uv.X(), uv.Y());
            const auto state = domain.classify(uv);
            if ((state != TopAbs_IN && state != TopAbs_ON) || position.Distance(source) > tolerance)
                continue;
            auto result = makeFacePoint(component_id, face_id, position, uv.X(), uv.Y());
            result.snap_kind = GeometryPointKind::Edge;
            result.snap_id = edge_id;
            result.snap_edge_parameter = parameter;
            return result;
        }
    } catch (const Standard_Failure& error) {
        logQueryFailure("projectEdgePointOnFace", component_id, edge_id, error);
        return std::nullopt;
    }
    return std::nullopt;
}

bool GeometryPointOps::isOnFaceBoundary(const TopoDS_Face& face, const std::array<double, 2>& uv)
{
    return SurfaceDomain(face).classify(gp_Pnt2d(uv[0], uv[1])) == TopAbs_ON;
}

std::vector<std::array<double, 2>> GeometryPointOps::resolveSurfacePathParameters(
    const TopoDS_Face& face, const std::vector<GeometryPoint>& points)
{
    const SurfaceDomain domain(face);
    std::vector<std::array<double, 2>> parameters;
    parameters.reserve(points.size());
    // 先校验持久载荷与几何的一致性，再用上一点连续展开周期参数。
    for (const auto& point : points) {
        if (!point.valid || point.support_kind != GeometryPointKind::Face
            || !isFinitePoint(point.world_pos) || !std::isfinite(point.face_uv[0]) || !std::isfinite(point.face_uv[1]))
            throw std::invalid_argument("Surface path contains an invalid face point");
        const auto normalized = domain.normalize(gp_Pnt2d(point.face_uv[0], point.face_uv[1]));
        std::array<double, 2> uv { normalized.X(), normalized.Y() };
        const auto position = domain.surface->Value(uv[0], uv[1]);
        if (position.Distance(gp_Pnt(point.world_pos[0], point.world_pos[1], point.world_pos[2])) > domain.tolerance)
            throw std::invalid_argument("Surface point no longer agrees with its face parameters");
        const auto state = domain.classify(gp_Pnt2d(uv[0], uv[1]));
        if (state != TopAbs_IN && state != TopAbs_ON)
            throw std::invalid_argument("Surface point lies outside the trimmed face");
        if (!parameters.empty()) {
            const auto& previous_position = points[parameters.size() - 1].world_pos;
            if (position.Distance(gp_Pnt(previous_position[0], previous_position[1], previous_position[2])) <= domain.tolerance)
                throw std::invalid_argument("Surface path contains repeated geometric points");
            if (domain.surface->IsUPeriodic())
                uv[0] += std::round((parameters.back()[0] - uv[0]) / domain.surface->UPeriod()) * domain.surface->UPeriod();
            if (domain.surface->IsVPeriodic())
                uv[1] += std::round((parameters.back()[1] - uv[1]) / domain.surface->VPeriod()) * domain.surface->VPeriod();
            const gp_Pnt2d previous(parameters.back()[0], parameters.back()[1]);
            try {
                validateDomainSegment(domain, previous, gp_Pnt2d(uv[0], uv[1]));
            } catch (const std::invalid_argument&) {
                // 部分周期面可能只允许较长的参数分支；优先连续分支，越域时寻找合法邻分支。
                const auto candidate_base = uv;
                bool found = false;
                double best_distance = std::numeric_limits<double>::max();
                const bool periodic_u = domain.surface->IsUPeriodic();
                const bool periodic_v = domain.surface->IsVPeriodic();
                for (int du = periodic_u ? -1 : 0; du <= (periodic_u ? 1 : 0); ++du) {
                    for (int dv = periodic_v ? -1 : 0; dv <= (periodic_v ? 1 : 0); ++dv) {
                        const gp_Pnt2d candidate(candidate_base[0] + (periodic_u ? du * domain.surface->UPeriod() : 0.0),
                            candidate_base[1] + (periodic_v ? dv * domain.surface->VPeriod() : 0.0));
                        try {
                            validateDomainSegment(domain, previous, candidate);
                            if (previous.SquareDistance(candidate) < best_distance) {
                                best_distance = previous.SquareDistance(candidate);
                                uv = { candidate.X(), candidate.Y() };
                                found = true;
                            }
                        } catch (const std::invalid_argument&) {
                            // 该周期分支越出修剪域，继续检查其余候选。
                        }
                    }
                }
                if (!found)
                    throw std::invalid_argument("Surface path leaves the trimmed face or crosses a hole");
            }
        }
        parameters.push_back(uv);
    }
    return parameters;
}

std::vector<std::array<double, 3>> GeometryPointOps::sampleSurfacePath(
    const TopoDS_Face& face, const std::vector<GeometryPoint>& points)
{
    const auto parameters = resolveSurfacePathParameters(face, points);
    std::vector<std::array<double, 3>> result;
    if (parameters.empty())
        return result;
    const auto surface = BRep_Tool::Surface(face);
    const auto first_position = surface->Value(parameters.front()[0], parameters.front()[1]);
    result.push_back({ first_position.X(), first_position.Y(), first_position.Z() });
    // 自适应细分控制真实曲面与预览折线的弦偏差；初始分段避免遗漏反弯。
    for (std::size_t index = 1; index < parameters.size(); ++index) {
        const auto& first = parameters[index - 1];
        const auto& last = parameters[index];
        const auto position = [&](double fraction) {
            return surface->Value(first[0] + (last[0] - first[0]) * fraction,
                first[1] + (last[1] - first[1]) * fraction);
        };
        const double tolerance = std::max(Precision::Confusion(), position(0).Distance(position(1)) * kPathChordDeviationFactor);
        std::function<void(double, double, int)> sample;
        sample = [&](double begin, double end, int depth) {
            const auto start = position(begin);
            const auto finish = position(end);
            const auto middle = position((begin + end) * 0.5);
            const gp_Pnt chord_middle((start.X() + finish.X()) * 0.5,
                (start.Y() + finish.Y()) * 0.5, (start.Z() + finish.Z()) * 0.5);
            if (middle.Distance(chord_middle) > tolerance && depth < kPathMaximumSubdivisionDepth) {
                sample(begin, (begin + end) * 0.5, depth + 1);
                sample((begin + end) * 0.5, end, depth + 1);
            } else {
                result.push_back({ finish.X(), finish.Y(), finish.Z() });
            }
        };
        for (int part = 0; part < kPathInitialSubdivisions; ++part)
            sample(static_cast<double>(part) / kPathInitialSubdivisions, static_cast<double>(part + 1) / kPathInitialSubdivisions, 0);
    }
    return result;
}
