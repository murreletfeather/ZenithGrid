#include "GeometryPointOps.h"
#include "GeometryRepairValidation.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Extrema_ExtPC.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <algorithm>
#include <cmath>

namespace {
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

}

std::optional<GeometryPoint> GeometryPointOps::projectEdgePointOnFace(
    const TopoDS_Face& face, const TopoDS_Edge& edge, double edge_parameter)
{
    if (face.IsNull() || edge.IsNull() || !std::isfinite(edge_parameter))
        return std::nullopt;
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
    const auto surface = BRep_Tool::Surface(face);
    if (surface.IsNull())
        return std::nullopt;
    double first_u, last_u, first_v, last_v;
    BRepTools::UVBounds(face, first_u, last_u, first_v, last_v);
    const double classification_tolerance = std::max(BRep_Tool::Tolerance(face),
        geometry::repair::PrecisionPolicy::fromShape(face).classification);
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
        // 将周期坐标归回目标面的参数域，再校验修剪边界和原边容差。
        const auto raw_uv = pcurve->Value(pc_parameter);
        const gp_Pnt2d uv(
            wrapParameter(raw_uv.X(), first_u, last_u, surface->IsUPeriodic(), surface->IsUPeriodic() ? surface->UPeriod() : 0.0),
            wrapParameter(raw_uv.Y(), first_v, last_v, surface->IsVPeriodic(), surface->IsVPeriodic() ? surface->VPeriod() : 0.0));
        const auto position = surface->Value(uv.X(), uv.Y());
        const auto state = BRepClass_FaceClassifier(face, uv, classification_tolerance).State();
        if ((state != TopAbs_IN && state != TopAbs_ON) || position.Distance(source) > tolerance)
            continue;
        GeometryPoint result;
        result.support_kind = GeometryPointKind::Face;
        result.world_pos = { position.X(), position.Y(), position.Z() };
        result.face_uv = { uv.X(), uv.Y() };
        result.valid = true;
        return result;
    }
    return std::nullopt;
}
