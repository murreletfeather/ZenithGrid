/**
 * @file MeasureManager.cpp
 * @brief 交互式测量管理器实现：两点成线显示长度，共端点两线自动显示夹角
 * @author 范成通 email 1941804585@qq.com
 */

#include "MeasureManager.h"
#include "CoincidentTopology.h"
#include "GeometryActorManagerSelectOp.h"
#include "GeometryActorSelectOp.h"
#include "MeshActorManagerSelectOp.h"
#include "Selection.h"

#include <IVtkTools_ShapePicker.hxx>
#include <NCollection_List.hxx>
#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCellArray.h>
#include <vtkDataSet.h>
#include <vtkHardwarePicker.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using Vec3 = std::array<double, 3>;

constexpr double kEps = 1e-9;

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

double dot(const Vec3& a, const Vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double length(const Vec3& v)
{
    return std::sqrt(dot(v, v));
}

Vec3 midpoint(const Vec3& a, const Vec3& b)
{
    return { (a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, (a[2] + b[2]) / 2.0 };
}

//! @brief 两向量夹角（度），与 MeasureHandler 的 angleBetween 一致
double angleBetween(const Vec3& u, const Vec3& v)
{
    const double lu = length(u);
    const double lv = length(v);
    if (lu < kEps || lv < kEps)
        return 0.0;

    double cos_theta = dot(u, v) / (lu * lv);
    cos_theta = std::max(-1.0, std::min(1.0, cos_theta));
    return std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
}

std::string fmtNum(double value, int precision)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return buf;
}

//! @brief 创建统一风格的 3D 面向相机文字标注（置顶层内不被模型遮挡）
vtkSmartPointer<vtkBillboardTextActor3D> makeTextActor(vtkRenderer* renderer,
    double r, double g, double b)
{
    auto text = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    text->SetInput("");
    vtkTextProperty* prop = text->GetTextProperty();
    prop->SetFontSize(16);
    prop->SetColor(r, g, b);
    prop->SetBold(true);
    prop->SetJustificationToCentered();
    prop->SetVerticalJustificationToCentered();
    prop->SetBackgroundColor(0.0, 0.0, 0.0);
    prop->SetBackgroundOpacity(0.5);
    text->PickableOff();
    text->SetVisibility(false);
    renderer->AddActor(text);
    return text;
}
}

MeasureManager::MeasureManager(vtkRenderer& renderer, vtkRenderer& overlay_renderer,
    MeshActorManagerSelectOp& mesh_op, GeometryActorManagerSelectOp& geom_op,
    IVtkTools_ShapePicker& geom_picker)
    : renderer_(&renderer)
    , overlay_renderer_(&overlay_renderer)
    , mesh_op_(&mesh_op)
    , geom_op_(&geom_op)
    , geom_picker_(&geom_picker)
{
    // 网格顶点拾取器：复用选择系统的 pick list，吸附网格顶点
    mesh_picker_ = vtkSmartPointer<vtkHardwarePicker>::New();
    mesh_picker_->SnapToMeshPointOn();
    mesh_picker_->SetPixelTolerance(5);
    mesh_picker_->PickFromListOn();
    mesh_op_->observePickList(mesh_picker_->GetPickList());

    // 几何顶点拾取与选择系统共用同一 picker 实例（由其持有者注册到 geom_op）：
    // 同一形状在多个 IVtkTools_ShapePicker 实例上激活同一选择模式时，
    // 仅最先激活的实例能正常拾取（OCC 侧行为），自建第二个实例会导致拾取恒失败

    // 端点标注（红色圆点，主渲染层）
    points_poly_ = vtkSmartPointer<vtkPolyData>::New();
    vtkNew<vtkPolyDataMapper> points_mapper;
    points_mapper->SetInputData(points_poly_);
    points_mapper->SetRelativeCoincidentTopologyPointOffsetParameter(highlight::POINT_UNITS);
    points_actor_ = vtkSmartPointer<vtkActor>::New();
    points_actor_->SetMapper(points_mapper);
    points_actor_->GetProperty()->SetColor(1.0, 0.0, 0.0);
    points_actor_->GetProperty()->SetPointSize(9.0);
    points_actor_->GetProperty()->SetRenderPointsAsSpheres(true);
    points_actor_->PickableOff();
    points_actor_->SetVisibility(false);
    renderer_->AddActor(points_actor_);

    // 已确认的测量线（绿色，主渲染层）
    lines_poly_ = vtkSmartPointer<vtkPolyData>::New();
    vtkNew<vtkPolyDataMapper> lines_mapper;
    lines_mapper->SetInputData(lines_poly_);
    lines_mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0, highlight::LINE_UNITS);
    lines_actor_ = vtkSmartPointer<vtkActor>::New();
    lines_actor_->SetMapper(lines_mapper);
    lines_actor_->GetProperty()->SetColor(0.2, 0.85, 0.2);
    lines_actor_->GetProperty()->SetLineWidth(2.0);
    lines_actor_->PickableOff();
    lines_actor_->SetVisibility(false);
    renderer_->AddActor(lines_actor_);

    // 悬停动态预览线（黄色虚线，主渲染层）
    hover_poly_ = vtkSmartPointer<vtkPolyData>::New();
    vtkNew<vtkPolyDataMapper> hover_mapper;
    hover_mapper->SetInputData(hover_poly_);
    hover_mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0, highlight::LINE_UNITS);
    hover_actor_ = vtkSmartPointer<vtkActor>::New();
    hover_actor_->SetMapper(hover_mapper);
    hover_actor_->GetProperty()->SetColor(1.0, 0.9, 0.1);
    hover_actor_->GetProperty()->SetLineWidth(2.0);
    hover_actor_->GetProperty()->SetLineStipplePattern(0xF0F0);
    hover_actor_->GetProperty()->SetLineStippleRepeatFactor(1);
    hover_actor_->PickableOff();
    hover_actor_->SetVisibility(false);
    renderer_->AddActor(hover_actor_);

    // 文字标注全部在叠加层：长度白色、夹角青色（池化）、悬停黄色
    hover_text_ = makeTextActor(overlay_renderer_, 1.0, 0.9, 0.1);
}

MeasureManager::~MeasureManager()
{
    mesh_op_->unobservePickList(mesh_picker_->GetPickList());
    if (renderer_) {
        renderer_->RemoveActor(points_actor_);
        renderer_->RemoveActor(lines_actor_);
        renderer_->RemoveActor(hover_actor_);
    }
    if (overlay_renderer_) {
        for (auto& t : len_texts_)
            overlay_renderer_->RemoveActor(t);
        for (auto& t : angle_texts_)
            overlay_renderer_->RemoveActor(t);
        overlay_renderer_->RemoveActor(hover_text_);
    }
}

void MeasureManager::setActive(bool on)
{
    if (active_ == on)
        return;
    active_ = on;
    clear();
    // 与选择系统共用 ShapePicker 模式：测量时切到几何顶点，退出时还原
    geom_op_->setShapePickerMode(on ? SelectMode::GeometryVertex : SelectMode::None);
}

void MeasureManager::pick(double posx, double posy)
{
    if (!active_)
        return;

    MeasurePoint p;
    if (!pickPoint(posx, posy, p))
        return;

    if (!pending_) {
        pending_ = p;
    } else if (samePoint(*pending_, p)) {
        pending_.reset(); // 同一点再点一次 = 取消本次起笔
    } else {
        addLine(*pending_, p);
        pending_.reset();
    }

    updateHoverActors(nullptr);
    updateActors();
    if (onChanged)
        onChanged();
}

void MeasureManager::hover(double posx, double posy)
{
    if (!active_ || !pending_)
        return;

    MeasurePoint p;
    const bool has_preview = pickPoint(posx, posy, p);
    updateHoverActors(has_preview ? &p.pos : nullptr);
}

void MeasureManager::clear()
{
    lines_.clear();
    angles_.clear();
    pending_.reset();
    updateHoverActors(nullptr);
    updateActors();
    if (onChanged)
        onChanged();
}

std::string MeasureManager::resultText() const
{
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (!out.empty())
            out += "\n";
        out += "L" + std::to_string(i + 1) + ": " + fmtNum(length(sub(lines_[i].b.pos, lines_[i].a.pos)), 6);
    }
    for (const MeasureAngle& ang : angles_) {
        if (!out.empty())
            out += "\n";
        out += "夹角(L" + std::to_string(ang.line1 + 1) + ",L" + std::to_string(ang.line2 + 1)
            + "): " + fmtNum(ang.angle, 6);
    }
    return out;
}

bool MeasureManager::samePoint(const MeasurePoint& a, const MeasurePoint& b)
{
    if (a.mesh_id >= 0 && a.mesh_id == b.mesh_id)
        return true;
    if (a.geom_id >= 0 && a.geom_id == b.geom_id)
        return true;
    // 同源顶点再次拾取坐标位级一致，作无 id 时的兜底
    return a.pos == b.pos;
}

bool MeasureManager::pickPoint(double posx, double posy, MeasurePoint& out)
{
    // 优先吸附网格顶点
    mesh_picker_->Pick(posx, posy, 0, renderer_);
    const vtkIdType picked_point_id = mesh_picker_->GetPointId();
    if (picked_point_id != -1) {
        vtkDataSet* data_set = mesh_picker_->GetDataSet();
        if (!data_set)
            return false;
        // 位置直接取数据集存储的精确坐标：同一顶点无论命中哪个 actor 都位级一致，
        // 保证跨次拾取判同一点可靠（GetPickPosition 是求交结果，不保证逐位相等）
        double p[3];
        data_set->GetPoint(picked_point_id, p);
        out.pos = { p[0], p[1], p[2] };
        out.mesh_id = -1;
        out.geom_id = -1;
        // 取全局顶点 id 作为附加判据（仿 VertexSelectorHighlight）
        auto ids = vtkIdTypeArray::SafeDownCast(
            data_set->GetPointData()->GetArray("vtkOriginalPointIds"));
        if (ids)
            out.mesh_id = ids->GetValue(picked_point_id);
        return true;
    }

    // 未命中网格再尝试几何顶点
    if (geom_picker_->Pick(posx, posy, 0.0, renderer_) > 0) {
        const auto& picked_shapes = geom_picker_->GetPickedShapesIds(false);
        if (!picked_shapes.IsEmpty()) {
            const IVtk_IdType shape_id = picked_shapes.First();
            auto component_id = geom_op_->getComponentIdByShapeId(shape_id);
            auto select_op = component_id ? geom_op_->getSelectOp(*component_id) : std::nullopt;
            if (select_op) {
                IVtk_IdType sub_id = -1;
                auto geom_id = select_op->resolvePickedSubshape(
                    geom_picker_, shape_id, SelectMode::GeometryVertex, sub_id);
                if (geom_id) {
                    if (auto p = select_op->vertexPoint(sub_id)) {
                        out.pos = *p;
                        out.mesh_id = -1;
                        out.geom_id = *geom_id;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void MeasureManager::addLine(const MeasurePoint& a, const MeasurePoint& b)
{
    // 与已有线完全重复（含反向）则忽略
    for (const MeasureLine& l : lines_) {
        if ((samePoint(l.a, a) && samePoint(l.b, b)) || (samePoint(l.a, b) && samePoint(l.b, a)))
            return;
    }

    // 与每条已有线做端点匹配，共端点即记录一组夹角
    const int new_idx = static_cast<int>(lines_.size());
    for (size_t i = 0; i < lines_.size(); ++i) {
        const MeasureLine& l = lines_[i];
        auto try_share = [&](const MeasurePoint& old_shared, const MeasurePoint& old_other,
                             const MeasurePoint& new_shared, const MeasurePoint& new_other) {
            if (!samePoint(old_shared, new_shared))
                return;
            MeasureAngle ang;
            ang.line1 = static_cast<int>(i);
            ang.line2 = new_idx;
            ang.at = old_shared.pos;
            ang.p = old_other.pos;
            ang.q = new_other.pos;
            ang.angle = angleBetween(sub(ang.p, ang.at), sub(ang.q, ang.at));
            angles_.push_back(ang);
        };
        try_share(l.a, l.b, a, b);
        try_share(l.a, l.b, b, a);
        try_share(l.b, l.a, a, b);
        try_share(l.b, l.a, b, a);
    }
    lines_.push_back({ a, b });
}

void MeasureManager::updateActors()
{
    // 重建端点与线段数据（含待定首点）
    vtkNew<vtkPoints> pts;
    vtkNew<vtkCellArray> verts;
    vtkNew<vtkCellArray> segs;
    for (const MeasureLine& l : lines_) {
        const vtkIdType ia = pts->InsertNextPoint(l.a.pos.data());
        verts->InsertNextCell(1, &ia);
        const vtkIdType ib = pts->InsertNextPoint(l.b.pos.data());
        verts->InsertNextCell(1, &ib);
        const vtkIdType ids[2] = { ia, ib };
        segs->InsertNextCell(2, ids);
    }
    if (pending_) {
        const vtkIdType ip = pts->InsertNextPoint(pending_->pos.data());
        verts->InsertNextCell(1, &ip);
    }
    points_poly_->SetPoints(pts);
    points_poly_->SetVerts(verts);
    lines_poly_->SetPoints(pts);
    lines_poly_->SetLines(segs);

    points_actor_->SetVisibility(pts->GetNumberOfPoints() > 0);
    lines_actor_->SetVisibility(!lines_.empty());

    // 长度文字：每线一个，放线段中点
    size_t li = 0;
    for (const MeasureLine& l : lines_) {
        vtkBillboardTextActor3D* t = getPooledText(len_texts_, li, 1.0, 1.0, 1.0);
        t->SetInput(("L: " + fmtNum(length(sub(l.b.pos, l.a.pos)), 2)).c_str());
        const Vec3 mid = midpoint(l.a.pos, l.b.pos);
        t->SetPosition(mid[0], mid[1], mid[2]);
        t->SetVisibility(true);
        ++li;
    }
    for (; li < len_texts_.size(); ++li)
        len_texts_[li]->SetVisibility(false);

    // 夹角文字：放共点沿角平分线偏移；同一点多个夹角按序号加大偏移防重叠
    size_t ai = 0;
    for (const MeasureAngle& ang : angles_) {
        vtkBillboardTextActor3D* t = getPooledText(angle_texts_, ai, 0.3, 0.9, 1.0);
        t->SetInput(("Ang: " + fmtNum(ang.angle, 2)).c_str());

        int stack = 0;
        for (size_t j = 0; j < ai; ++j) {
            if (angles_[j].at == ang.at)
                ++stack;
        }

        const Vec3 u = sub(ang.p, ang.at);
        const Vec3 v = sub(ang.q, ang.at);
        const double lu = length(u);
        const double lv = length(v);
        Vec3 dir { 0.0, 0.0, 0.0 };
        if (lu > kEps && lv > kEps) {
            // 角平分线方向；u、v 近反向（180°）时退化为两端点中点方向
            const Vec3 s { u[0] / lu + v[0] / lv, u[1] / lu + v[1] / lv, u[2] / lu + v[2] / lv };
            const double ls = length(s);
            if (ls > kEps) {
                dir = { s[0] / ls, s[1] / ls, s[2] / ls };
            } else {
                const Vec3 d = sub(midpoint(ang.p, ang.q), ang.at);
                const double ld = length(d);
                dir = ld > kEps ? Vec3 { d[0] / ld, d[1] / ld, d[2] / ld }
                                : Vec3 { u[0] / lu, u[1] / lu, u[2] / lu };
            }
        }
        const double dist = 0.25 * std::min(lu, lv) * (1.0 + 0.3 * stack);
        t->SetPosition(ang.at[0] + dir[0] * dist, ang.at[1] + dir[1] * dist, ang.at[2] + dir[2] * dist);
        t->SetVisibility(true);
        ++ai;
    }
    for (; ai < angle_texts_.size(); ++ai)
        angle_texts_[ai]->SetVisibility(false);
}

void MeasureManager::updateHoverActors(const Vec3* snapped)
{
    if (!snapped || !pending_) {
        hover_actor_->SetVisibility(false);
        hover_text_->SetVisibility(false);
        return;
    }

    // 动态虚线：待定首点到吸附点
    vtkNew<vtkPoints> pts;
    pts->InsertNextPoint(pending_->pos.data());
    pts->InsertNextPoint(snapped->data());
    vtkNew<vtkCellArray> segs;
    segs->InsertNextCell(2);
    segs->InsertCellPoint(0);
    segs->InsertCellPoint(1);
    hover_poly_->SetPoints(pts);
    hover_poly_->SetLines(segs);
    hover_actor_->SetVisibility(true);

    hover_text_->SetInput(("L: " + fmtNum(length(sub(*snapped, pending_->pos)), 2)).c_str());
    const Vec3 mid = midpoint(pending_->pos, *snapped);
    hover_text_->SetPosition(mid[0], mid[1], mid[2]);
    hover_text_->SetVisibility(true);
}

vtkBillboardTextActor3D* MeasureManager::getPooledText(
    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>>& pool, size_t index,
    double r, double g, double b)
{
    while (pool.size() <= index)
        pool.push_back(makeTextActor(overlay_renderer_, r, g, b));
    return pool[index];
}
