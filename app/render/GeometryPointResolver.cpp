/**
 * @file GeometryPointResolver.cpp
 * @brief 精确边位置拾取与遮挡判断，不修改选择集合。
 */
#include "GeometryPointResolver.h"

#include "GeometryActorManagerSelectOp.h"
#include "GeometryActorSelectOp.h"
#include "GeometryPointOps.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_Shape.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Surface.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <spdlog/spdlog.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>

namespace {
//! @brief 鼠标到边的最大逻辑像素距离。
constexpr double kSnapRadius = 10.0;
//! @brief 边候选的最小区间采样数与整条曲线的初始采样预算，仅用于屏幕粗筛。
constexpr int kMinimumEdgeSamples = 8;
constexpr int kEdgeSampleBudget = 64;
//! @brief 反投影齐次坐标和 AABB 平行射线判定的数值下限。
constexpr double kHomogeneousCoordinateEpsilon = 1e-15;
constexpr double kParallelRayEpsilon = 1e-14;
//! @brief 遮挡深度比较的绝对/相对余量，不作为拓扑建模容差。
constexpr double kOcclusionAbsoluteTolerance = 1e-6;
constexpr double kOcclusionRelativeTolerance = 1e-7;
//! @brief 曲线粗筛多保留一个渲染像素；最终命中仍严格使用吸附半径。
constexpr double kEdgeCandidatePixelMargin = 1.0;
//! @brief 精确曲线屏幕距离的三分收缩次数。
constexpr int kEdgeRefineIterations = 28;
/** @brief 仅在启用 debug 时记录查询异常，每个渲染线程每秒最多一条，避免悬停刷屏。 */
void logQueryFailure(const char* operation, Index component_id, Index support_id, const char* message)
{
    auto* logger = spdlog::default_logger_raw();
    if (!logger || !logger->should_log(spdlog::level::debug))
        return;
    thread_local std::chrono::steady_clock::time_point next_report;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_report)
        return;
    next_report = now + std::chrono::seconds(1);
    logger->debug("GeometryPointResolver: {} component={} support={}: {}", operation,
        component_id, support_id, message ? message : "Unknown exception");
}

/** @brief 为精确曲线区间构造保守包络；无法保证包络时返回空盒，后续不得据此排除。 */
Bnd_Box curveSegmentBounds(const BRepAdaptor_Curve& curve, double first, double last)
{
    Bnd_Box bounds;
    switch (curve.GetType()) {
    case GeomAbs_Line:
    case GeomAbs_Circle:
    case GeomAbs_Ellipse:
    case GeomAbs_Hyperbola:
    case GeomAbs_Parabola:
        BndLib_Add3dCurve::Add(curve, first, last, Precision::Confusion(), bounds);
        break;
    case GeomAbs_BezierCurve: {
        // 只分割独立副本，禁止修改适配器可能共享的源几何和 undo 前像。
        auto segment = occ::down_cast<Geom_BezierCurve>(curve.Bezier()->Copy());
        segment->Segment(first, last);
        for (int pole = 1; pole <= segment->NbPoles(); ++pole)
            bounds.Add(segment->Pole(pole));
        break;
    }
    case GeomAbs_BSplineCurve: {
        auto segment = occ::down_cast<Geom_BSplineCurve>(curve.BSpline()->Copy());
        segment->Segment(first, last);
        for (int pole = 1; pole <= segment->NbPoles(); ++pole)
            bounds.Add(segment->Pole(pole));
        break;
    }
    default:
        return bounds;
    }
    // 样条使用控制点凸包，不以另一套有限采样结果缩小包络。
    bounds.Enlarge(std::max(Precision::Confusion(), curve.Tolerance()));
    return bounds;
}

/** @brief 可见组件中的一个子形状快照，曲线采样只用来筛选屏幕候选。 */
struct ShapeSnapshot {
    Index component_id;
    Index id;
    TopoDS_Shape shape;
    std::vector<std::pair<double, std::array<double, 3>>> samples;
    std::vector<Bnd_Box> segment_bounds; //> 与相邻采样参数一一对应，共用候选快照的刷新周期。
    std::unique_ptr<IntCurvesFace_ShapeIntersector> intersector;
    std::array<double, 3> minimum { }, maximum { };
    bool has_bounds { false };
};

/** @brief 屏幕射线与点深度共用的三维距离。 */
double distanceSquared(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    double result = 0;
    for (size_t i = 0; i < 3; ++i)
        result += (a[i] - b[i]) * (a[i] - b[i]);
    return result;
}

}

/** @brief 渲染线程独占的场景候选；不保存参数监听状态。 */
struct GeometryPointResolver::Impl {
    vtkRenderer& renderer;
    GeometryActorManagerSelectOp& geometry_op;
    std::vector<ShapeSnapshot> edges, faces;
    bool ready { false };

    /** @brief 仅绑定场景，缓存按刷新或第一次查询建立。 */
    Impl(vtkRenderer& renderer_value, GeometryActorManagerSelectOp& op)
        : renderer(renderer_value)
        , geometry_op(op)
    {
    }

    /** @brief 位置查询可独立于点采集会话使用，未装配时才构建缓存。 */
    void ensureShapes()
    {
        if (!ready)
            collectShapes();
    }

    /** @brief 只从渲染 Actor 获取独立身份与拓扑快照。 */
    void collectShapes()
    {
        ready = false;
        edges.clear();
        faces.clear();
        for (Index component_id : geometry_op.getAllComponentIds()) {
            auto op = geometry_op.getSelectOp(component_id);
            if (!op)
                continue;
            for (auto& [id, shape] : op->subshapes(TopAbs_FACE)) {
                ShapeSnapshot face { .component_id = component_id, .id = id, .shape = shape };
                // 修剪面算法与包围盒随几何快照装配，切换输入不重建；悬停仅筛选并求交。
                const double tolerance = std::max(Precision::Confusion(), BRep_Tool::Tolerance(TopoDS::Face(shape)));
                face.intersector = std::make_unique<IntCurvesFace_ShapeIntersector>();
                face.intersector->Load(shape, tolerance);
                Bnd_Box bounds;
                BRepBndLib::AddOptimal(shape, bounds, false, true);
                bounds.SetGap(tolerance);
                if (!bounds.IsVoid() && !bounds.IsOpen()) {
                    bounds.Get(face.minimum[0], face.minimum[1], face.minimum[2],
                        face.maximum[0], face.maximum[1], face.maximum[2]);
                    face.has_bounds = true;
                }
                faces.push_back(std::move(face));
            }
            for (auto& [id, shape] : op->subshapes(TopAbs_EDGE)) {
                ShapeSnapshot snapshot { .component_id = component_id, .id = id, .shape = shape };
                // 按连续区间分段采样；最终命中从精确曲线求值，不使用折线插值坐标。
                try {
                    BRepAdaptor_Curve curve(TopoDS::Edge(shape));
                    const int count = curve.NbIntervals(GeomAbs_CN);
                    TColStd_Array1OfReal intervals(1, count + 1);
                    curve.Intervals(intervals, GeomAbs_CN);
                    const int subdivisions = std::max(kMinimumEdgeSamples, kEdgeSampleBudget / std::max(1, count));
                    for (int interval = 1; interval <= count; ++interval) {
                        for (int sample = 0; sample <= subdivisions; ++sample) {
                            if (interval > 1 && sample == 0)
                                continue;
                            const double parameter = intervals(interval)
                                + (intervals(interval + 1) - intervals(interval)) * sample / subdivisions;
                            const gp_Pnt point = curve.Value(parameter);
                            snapshot.samples.push_back({ parameter, { point.X(), point.Y(), point.Z() } });
                        }
                    }
                    // 包络只在场景采样时计算，悬停不重复执行 OCC 曲线裁分。
                    Bnd_Box all_bounds;
                    bool bounded = true;
                    for (size_t sample = 1; sample < snapshot.samples.size(); ++sample) {
                        Bnd_Box bounds;
                        try {
                            bounds = curveSegmentBounds(curve, snapshot.samples[sample - 1].first,
                                snapshot.samples[sample].first);
                        } catch (const Standard_Failure& error) {
                            logQueryFailure("boundEdge", component_id, id, error.GetMessageString());
                        }
                        if (bounds.IsVoid() || bounds.IsOpen())
                            bounded = false;
                        else
                            all_bounds.Add(bounds);
                        snapshot.segment_bounds.push_back(bounds);
                    }
                    if (bounded && !all_bounds.IsVoid() && !all_bounds.IsOpen()) {
                        all_bounds.Get(snapshot.minimum[0], snapshot.minimum[1], snapshot.minimum[2],
                            snapshot.maximum[0], snapshot.maximum[1], snapshot.maximum[2]);
                        snapshot.has_bounds = true;
                    }
                } catch (const Standard_Failure& error) {
                    // 无法求值的边跳过采样，不能制造任意曲线参数。
                    logQueryFailure("sampleEdge", component_id, id, error.GetMessageString());
                }
                edges.push_back(std::move(snapshot));
            }
        }
        ready = true;
    }

    /** @brief 每次命中重新检查实际可见性，覆盖采集中途隐藏和样式切换。 */
    bool visible(Index component_id) const
    {
        auto op = geometry_op.getSelectOp(component_id);
        return op && op->isVisible();
    }

    /** @brief 世界点投到 VTK 像素空间，并拒绝相机裁剪范围以外的点。 */
    std::optional<std::array<double, 3>> display(const std::array<double, 3>& world) const
    {
        renderer.SetWorldPoint(world[0], world[1], world[2], 1);
        renderer.WorldToDisplay();
        const double* point = renderer.GetDisplayPoint();
        if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || point[2] < 0 || point[2] > 1)
            return std::nullopt;
        return std::array<double, 3> { point[0], point[1], point[2] };
    }

    /** @brief 盒投影到鼠标的保守距离；跨相机裁剪范围时保留候选，交给精确求解判定。 */
    double projectedBoundsDistance(const std::array<double, 3>& minimum, const std::array<double, 3>& maximum,
        double x, double y) const
    {
        double left = std::numeric_limits<double>::infinity(), right = -left;
        double bottom = left, top = right;
        for (int corner = 0; corner < 8; ++corner) {
            const auto point = display({ corner & 1 ? maximum[0] : minimum[0],
                corner & 2 ? maximum[1] : minimum[1], corner & 4 ? maximum[2] : minimum[2] });
            if (!point)
                return 0;
            left = std::min(left, (*point)[0]);
            right = std::max(right, (*point)[0]);
            bottom = std::min(bottom, (*point)[1]);
            top = std::max(top, (*point)[1]);
        }
        return std::hypot(std::max({ left - x, 0.0, x - right }), std::max({ bottom - y, 0.0, y - top }));
    }

    /** @brief 反投影近/远裁剪平面，适用于正交和透视相机。 */
    bool ray(double x, double y, std::array<double, 3>& origin, std::array<double, 3>& direction) const
    {
        std::array<double, 3> far_point { };
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            renderer.SetDisplayPoint(x, y, endpoint);
            renderer.DisplayToWorld();
            const double* point = renderer.GetWorldPoint();
            if (!std::isfinite(point[3]) || std::abs(point[3]) < kHomogeneousCoordinateEpsilon)
                return false;
            auto& output = endpoint == 0 ? origin : far_point;
            for (size_t i = 0; i < 3; ++i)
                output[i] = point[i] / point[3];
        }
        const double length = std::sqrt(distanceSquared(origin, far_point));
        if (length <= 0)
            return false;
        for (size_t i = 0; i < 3; ++i)
            direction[i] = (far_point[i] - origin[i]) / length;
        return true;
    }

    /** @brief 按候选自身像素检查遮挡，仅接纳同一拓扑来源在相邻面上的局部表征误差。 */
    bool unobstructed(const std::array<double, 3>& point, const ShapeSnapshot& edge_source,
        double edge_parameter) const
    {
        auto screen = display(point);
        if (!screen)
            return false;
        std::array<double, 3> origin, direction;
        if (!ray((*screen)[0], (*screen)[1], origin, direction))
            return false;
        const double candidate_distance = std::sqrt(distanceSquared(point, origin));
        const double tolerance = std::max(kOcclusionAbsoluteTolerance, candidate_distance * kOcclusionRelativeTolerance);
        for (const auto& face : faces) {
            auto op = geometry_op.getSelectOp(face.component_id);
            if (!op || !op->isVisible())
                continue;
            // 透明和线框显示允许看到后方几何，不能把这些面当作不透明遮挡物。
            auto& actor = op->getPolyActor();
            auto* property = actor.GetProperty();
            if (!actor.GetVisibility() || property->GetRepresentation() != VTK_SURFACE
                || property->GetOpacity() < 1.0)
                continue;
            auto hit = intersect(face, origin, direction);
            if (hit && std::sqrt(distanceSquared(*hit, origin)) + tolerance < candidate_distance) {
                if (edge_source.component_id == face.component_id) {
                    // 限定已验证 pcurve 的局部邻域；斜射线长度会放大间隙，须沿局部面法向核对。
                    const auto admits_representation = [&](const GeometryPoint& boundary, double source_tolerance) {
                        const double representation_tolerance = std::max(Precision::Confusion(), source_tolerance) + tolerance;
                        if (distanceSquared(*hit, boundary.world_pos) > representation_tolerance * representation_tolerance)
                            return false;
                        gp_Pnt ignored;
                        gp_Vec du, dv;
                        BRep_Tool::Surface(TopoDS::Face(face.shape))->D1(boundary.face_uv[0], boundary.face_uv[1], ignored, du, dv);
                        const auto normal = du.Crossed(dv);
                        const double magnitude = normal.Magnitude();
                        if (std::isfinite(magnitude) && magnitude > std::numeric_limits<double>::min()) {
                            const gp_Vec gap(gp_Pnt(point[0], point[1], point[2]),
                                gp_Pnt((*hit)[0], (*hit)[1], (*hit)[2]));
                            const double normal_gap = std::abs(gap.Dot(normal) / magnitude);
                            return std::isfinite(normal_gap) && normal_gap <= representation_tolerance;
                        }
                        return false;
                    };
                    const auto edge = TopoDS::Edge(edge_source.shape);
                    const auto boundary = GeometryPointOps::projectEdgePointOnFace(TopoDS::Face(face.shape), edge, edge_parameter);
                    if (boundary && admits_representation(*boundary, BRep_Tool::Tolerance(edge)))
                        continue;
                }
                return false;
            }
        }
        return true;
    }

    /** @brief 用射线与 AABB 先筛选，再复用该面的 OCC 修剪面相交算法。 */
    std::optional<std::array<double, 3>> intersect(const ShapeSnapshot& face,
        const std::array<double, 3>& origin, const std::array<double, 3>& direction) const
    {
        if (face.has_bounds) {
            double first = 0, last = std::numeric_limits<double>::max();
            for (size_t axis = 0; axis < 3; ++axis) {
                if (std::abs(direction[axis]) < kParallelRayEpsilon) {
                    if (origin[axis] < face.minimum[axis] || origin[axis] > face.maximum[axis])
                        return std::nullopt;
                } else {
                    const double a = (face.minimum[axis] - origin[axis]) / direction[axis];
                    const double b = (face.maximum[axis] - origin[axis]) / direction[axis];
                    first = std::max(first, std::min(a, b));
                    last = std::min(last, std::max(a, b));
                    if (first > last)
                        return std::nullopt;
                }
            }
        }
        face.intersector->Perform(gp_Lin(gp_Pnt(origin[0], origin[1], origin[2]),
                                      gp_Dir(direction[0], direction[1], direction[2])),
            0, std::numeric_limits<double>::max());
        if (!face.intersector->IsDone())
            return std::nullopt;
        double parameter = std::numeric_limits<double>::infinity();
        int hit = 0;
        for (int index = 1; index <= face.intersector->NbPnt(); ++index) {
            const double candidate = face.intersector->WParameter(index);
            if (candidate >= 0 && candidate < parameter) {
                parameter = candidate;
                hit = index;
            }
        }
        if (!hit)
            return std::nullopt;
        const auto& position = face.intersector->Pnt(hit);
        return std::array<double, 3> { position.X(), position.Y(), position.Z() };
    }

    /** @brief 按屏幕距离精化曲线参数；被遮挡的分支失败后仍尝试其余候选。 */
    std::optional<std::pair<GeometryPoint, double>> edgePoint(const ShapeSnapshot& edge, double x, double y,
        double pixel_ratio) const
    {
        const double radius = std::max(1.0, kSnapRadius * pixel_ratio);
        const double candidate_radius = radius + kEdgeCandidatePixelMargin;
        if (edge.has_bounds && projectedBoundsDistance(edge.minimum, edge.maximum, x, y) > candidate_radius)
            return std::nullopt;
        std::vector<std::pair<double, size_t>> candidates;
        // 折线只定位参数区间，不作为最终几何坐标来源。
        for (size_t i = 1; i < edge.samples.size(); ++i) {
            const auto first = display(edge.samples[i - 1].second);
            const auto last = display(edge.samples[i].second);
            double distance = std::numeric_limits<double>::infinity();
            if (first && last) {
                const double dx = (*last)[0] - (*first)[0], dy = (*last)[1] - (*first)[1];
                const double denominator = dx * dx + dy * dy;
                const double t = denominator > 0 ? std::clamp(((x - (*first)[0]) * dx + (y - (*first)[1]) * dy) / denominator, 0.0, 1.0) : 0;
                distance = std::hypot((*first)[0] + t * dx - x, (*first)[1] + t * dy - y);
            }
            // 弦距仅作快速放行和排序；放大后曲线可远离弦，只能用真实包络排除。
            if (distance > candidate_radius && i - 1 < edge.segment_bounds.size()) {
                const auto& bounds = edge.segment_bounds[i - 1];
                if (!bounds.IsVoid() && !bounds.IsOpen()) {
                    std::array<double, 3> minimum, maximum;
                    bounds.Get(minimum[0], minimum[1], minimum[2], maximum[0], maximum[1], maximum[2]);
                    if (projectedBoundsDistance(minimum, maximum, x, y) > candidate_radius)
                        continue;
                }
            }
            candidates.emplace_back(distance, i);
        }
        std::sort(candidates.begin(), candidates.end());
        if (candidates.empty())
            return std::nullopt;
        BRepAdaptor_Curve curve(TopoDS::Edge(edge.shape));
        // 参数收敛同时满足曲线的三维精度预算后才早停，不以像素误差代替几何精度。
        const double parameter_tolerance = std::min(Precision::PConfusion(), curve.Resolution(Precision::Confusion()));
        const auto distance = [&](double parameter) {
            const gp_Pnt p = curve.Value(parameter);
            const auto screen = display({ p.X(), p.Y(), p.Z() });
            return screen ? std::hypot((*screen)[0] - x, (*screen)[1] - y) : std::numeric_limits<double>::infinity();
        };
        for (const auto& [approximate_distance, segment] : candidates) {
            double left = edge.samples[segment - 1].first, right = edge.samples[segment].first;
            // 保留原精化上限，已收敛的小区间不再重复求值；遮挡失败仍检查其余分支。
            for (int iteration = 0; iteration < kEdgeRefineIterations && right - left > parameter_tolerance; ++iteration) {
                const double a = left + (right - left) / 3, b = right - (right - left) / 3;
                if (distance(a) < distance(b))
                    right = b;
                else
                    left = a;
            }
            double parameter = (left + right) * .5;
            for (double endpoint : { edge.samples[segment - 1].first, edge.samples[segment].first })
                if (distance(endpoint) < distance(parameter))
                    parameter = endpoint;
            const double precise_distance = distance(parameter);
            if (precise_distance > radius)
                continue;
            const gp_Pnt position = curve.Value(parameter);
            GeometryPoint point;
            point.component_id = edge.component_id;
            point.support_kind = GeometryPointKind::Edge;
            point.support_id = edge.id;
            point.world_pos = { position.X(), position.Y(), position.Z() };
            point.edge_parameter = parameter;
            point.valid = true;
            if (!unobstructed(point.world_pos, edge, parameter))
                continue;
            return std::pair { point, precise_distance };
        }
        return std::nullopt;
    }

    /** @brief 从所有可见边选取半径内最近的精确位置，未命中不制造点。 */
    GeometryPoint resolve(double x, double y, double pixel_ratio) const
    {
        GeometryPoint result;
        double distance = std::max(1.0, kSnapRadius * pixel_ratio);
        for (const auto& edge : edges) {
            if (!visible(edge.component_id))
                continue;
            const auto candidate = edgePoint(edge, x, y, pixel_ratio);
            if (candidate && candidate->second <= distance) {
                result = candidate->first;
                distance = candidate->second;
            }
        }
        return result;
    }
};

GeometryPointResolver::GeometryPointResolver(vtkRenderer& renderer, GeometryActorManagerSelectOp& op)
    : impl_(std::make_unique<Impl>(renderer, op))
{
}

GeometryPointResolver::~GeometryPointResolver() = default;

void GeometryPointResolver::clear()
{
    impl_->ready = false;
    impl_->edges.clear();
    impl_->faces.clear();
}

GeometryPoint GeometryPointResolver::resolvePoint(double x, double y, double pixel_ratio) const
{
    try {
        impl_->ensureShapes();
        return impl_->resolve(x, y, std::max(1.0, pixel_ratio));
    } catch (const Standard_Failure& error) {
        logQueryFailure("resolvePoint", -1, -1, error.GetMessageString());
    } catch (const std::exception& error) {
        logQueryFailure("resolvePoint", -1, -1, error.what());
    }
    return { };
}
