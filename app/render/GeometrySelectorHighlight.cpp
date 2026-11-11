#include "GeometrySelectorHighlight.h"
#include "CoincidentTopology.h"
#include "Core.h"
#include "GeometryActorSelectOp.h"

#include <IVtkTools_SubPolyDataFilter.hxx>
#include <IVtk_Types.hxx>
#include <NCollection_List.hxx>
#include <vtkCellArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkUnsignedCharArray.h>
#include <vtkActor.h>
#include <vtkMapper.h>
#include <vtkPartitionedDataSet.h>
#include <vtkProperty.h>

GeometryPointSelectorHighlight::GeometryPointSelectorHighlight(vtkRenderer& renderer)
    : renderer_(&renderer)
    , actor_(vtkSmartPointer<vtkActor>::New())
    , data_(vtkSmartPointer<vtkPolyData>::New())
{
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(data_);
    mapper->SetRelativeCoincidentTopologyPointOffsetParameter(highlight::POINT_UNITS);
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray("Colors");
    actor_->SetMapper(mapper);
    actor_->GetProperty()->SetPointSize(9.0);
    actor_->GetProperty()->SetRenderPointsAsSpheres(true);
    actor_->PickableOff();
    actor_->SetVisibility(false);
    renderer_->AddActor(actor_);
}

GeometryPointSelectorHighlight::~GeometryPointSelectorHighlight()
{
    renderer_->RemoveActor(actor_);
}

void GeometryPointSelectorHighlight::select(const GeometryPoint& point)
{
    if (!point.valid)
        return;
    selection_ = Selection::create<ElementEnum::GeometryPoint>({ point }, point.component_id);
    hover_ = { };
    updateHighlight();
}

void GeometryPointSelectorHighlight::hover(const GeometryPoint& point)
{
    hover_ = point;
    updateHighlight();
}

void GeometryPointSelectorHighlight::setComponentVisible(Index component_id, bool visible)
{
    if (visible)
        hidden_components_.erase(component_id);
    else
        hidden_components_.insert(component_id);
    updateHighlight();
}

void GeometryPointSelectorHighlight::clear()
{
    selection_ = Selection::create<ElementEnum::GeometryPoint>({ });
    hover_ = { };
    updateHighlight();
}

void GeometryPointSelectorHighlight::disableHighlight()
{
    visible_ = false;
    updateHighlight();
}

void GeometryPointSelectorHighlight::enableHighlight()
{
    visible_ = true;
    updateHighlight();
}

Selection GeometryPointSelectorHighlight::get() const
{
    return selection_;
}

void GeometryPointSelectorHighlight::updateHighlight()
{
    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    vtkNew<vtkCellArray> vertices;
    vtkNew<vtkUnsignedCharArray> colors;
    colors->SetName("Colors");
    colors->SetNumberOfComponents(3);
    const auto append = [&](const GeometryPoint& point, unsigned char r, unsigned char g, unsigned char b) {
        if (!point.valid || hidden_components_.contains(point.component_id))
            return;
        const auto id = points->InsertNextPoint(point.world_pos.data());
        vertices->InsertNextCell(1, &id);
        const unsigned char color[] { r, g, b };
        colors->InsertNextTypedTuple(color);
    };
    for (const auto& point : *selection_.get<ElementEnum::GeometryPoint>())
        append(point, 51, 229, 89);
    append(hover_, 255, 165, 25);
    data_->SetPoints(points);
    data_->SetVerts(vertices);
    data_->GetPointData()->SetScalars(colors);
    data_->Modified();
    actor_->SetVisibility(visible_ && points->GetNumberOfPoints() > 0);
}

// 把共享高亮 actor 挂到当前 selector 的 mapper 上，并按模式套用完整样式（避免残留上一模式属性）
static void mountLineHighlight(vtkActor& actor, vtkMapper& mapper)
{
    actor.SetMapper(&mapper);

    // 相对显示 mapper 默认值
    mapper.SetRelativeCoincidentTopologyLineOffsetParameters(0, highlight::LINE_UNITS);
    mapper.SetRelativeCoincidentTopologyPointOffsetParameter(highlight::POINT_UNITS);

    vtkNew<vtkProperty> prop;
    prop->SetColor(1.0, 0.0, 0.0);
    prop->SetOpacity(0.5);
    prop->RenderLinesAsTubesOn();
    prop->SetLineWidth(3.0);
    prop->SetPointSize(8.0);
    prop->LightingOff();

    actor.SetProperty(prop);
}

static void mountFaceHighlight(vtkActor& actor, vtkMapper& mapper)
{
    actor.SetMapper(&mapper);

    // 相对显示 mapper 默认值
    mapper.SetRelativeCoincidentTopologyPolygonOffsetParameters(0, highlight::POLYGON_UNITS);

    vtkNew<vtkProperty> prop;
    prop->SetColor(1.0, 0.0, 0.0);
    prop->SetOpacity(1.0);
    actor.SetProperty(prop);
}

static void updateFilterAndNotify(IVtkTools_SubPolyDataFilter* filter, const NCollection_List<IVtk_IdType>& ids,
    vtkPartitionedDataSet* data)
{
    if (filter) {
        filter->SetData(ids);
        filter->Modified();
        filter->Update();
    }
    if (data)
        data->Modified();
}

// ─── Face ──────────────────────────────────────────────

GeometryFaceSelectorHighlight::GeometryFaceSelectorHighlight(
    vtkPartitionedDataSet& highlight_data, unsigned int partition_id,
    GeometryActorSelectOp select_op)
    : highlight_data_(&highlight_data)
    , partition_id_(partition_id)
    , select_op_(std::move(select_op))
{
    hl_filter_ = select_op_.buildHighlight(SelectMode::GeometryFace);
    if (hl_filter_) {
        hl_filter_->Update();
        highlight_data_->SetPartition(partition_id_, hl_filter_->GetOutput());
    }
}

GeometryFaceSelectorHighlight::~GeometryFaceSelectorHighlight()
{
    highlight_data_->SetPartition(partition_id_, nullptr);
}

void GeometryFaceSelectorHighlight::clear()
{
    disableHighlight();
    selections_.clear();
}

void GeometryFaceSelectorHighlight::disableHighlight()
{
    updateFilterAndNotify(hl_filter_, NCollection_List<IVtk_IdType>(), highlight_data_);
}

void GeometryFaceSelectorHighlight::enableHighlight()
{
    NCollection_List<IVtk_IdType> ids;
    for (const auto& [sid, gid] : selections_)
        ids.Append(sid);
    updateFilterAndNotify(hl_filter_, ids, highlight_data_);
}

GeometrySelectionVtk GeometryFaceSelectorHighlight::get() const
{
    GeometrySelectionVtk s;
    s.setType(ElementEnum::GeometryFace);
    s.component_id = -1;
    for (const auto& [subId, geomId] : selections_)
        s.ids().push_back(geomId);
    return s;
}

void GeometryFaceSelectorHighlight::toggle(IVtk_IdType subId, Index geomId, SelectOp op)
{
    auto it = selections_.find(subId);
    const bool present = (it != selections_.end());
    switch (op) {
    case SelectOp::Append:
        if (!present) selections_.emplace(subId, geomId);
        break;
    case SelectOp::Remove:
        if (present) selections_.erase(it);
        break;
    case SelectOp::Toggle:
        if (present) selections_.erase(it);
        else selections_.emplace(subId, geomId);
        break;
    }

    enableHighlight();
}

void GeometryFaceSelectorHighlight::setupHighlightStyle(vtkActor& actor, vtkMapper& mapper)
{
    mountFaceHighlight(actor, mapper);
}

// ─── Edge ──────────────────────────────────────────────

GeometryEdgeSelectorHighlight::GeometryEdgeSelectorHighlight(
    vtkPartitionedDataSet& highlight_data, unsigned int partition_id,
    GeometryActorSelectOp select_op)
    : highlight_data_(&highlight_data)
    , partition_id_(partition_id)
    , select_op_(std::move(select_op))
{
    hl_filter_ = select_op_.buildHighlight(SelectMode::GeometryEdge);
    if (hl_filter_) {
        hl_filter_->Update();
        highlight_data_->SetPartition(partition_id_, hl_filter_->GetOutput());
    }
}

GeometryEdgeSelectorHighlight::~GeometryEdgeSelectorHighlight()
{
    highlight_data_->SetPartition(partition_id_, nullptr);
}

void GeometryEdgeSelectorHighlight::clear()
{
    disableHighlight();
    selections_.clear();
}

void GeometryEdgeSelectorHighlight::disableHighlight()
{
    updateFilterAndNotify(hl_filter_, NCollection_List<IVtk_IdType>(), highlight_data_);
}

void GeometryEdgeSelectorHighlight::enableHighlight()
{
    NCollection_List<IVtk_IdType> ids;
    for (const auto& [sid, gid] : selections_)
        ids.Append(sid);
    updateFilterAndNotify(hl_filter_, ids, highlight_data_);
}

GeometrySelectionVtk GeometryEdgeSelectorHighlight::get() const
{
    GeometrySelectionVtk s;
    s.setType(ElementEnum::GeometryEdge);
    s.component_id = -1;
    for (const auto& [subId, geomId] : selections_)
        s.ids().push_back(geomId);
    return s;
}

void GeometryEdgeSelectorHighlight::toggle(IVtk_IdType subId, Index geomId, SelectOp op)
{
    auto it = selections_.find(subId);
    const bool present = (it != selections_.end());
    switch (op) {
    case SelectOp::Append:
        if (!present) selections_.emplace(subId, geomId);
        break;
    case SelectOp::Remove:
        if (present) selections_.erase(it);
        break;
    case SelectOp::Toggle:
        if (present) selections_.erase(it);
        else selections_.emplace(subId, geomId);
        break;
    }

    enableHighlight();
}

void GeometryEdgeSelectorHighlight::setupHighlightStyle(vtkActor& actor, vtkMapper& mapper)
{
    mountLineHighlight(actor, mapper);
}

// ─── Vertex ────────────────────────────────────────────

GeometryVertexSelectorHighlight::GeometryVertexSelectorHighlight(
    vtkPartitionedDataSet& highlight_data, unsigned int partition_id,
    GeometryActorSelectOp select_op)
    : highlight_data_(&highlight_data)
    , partition_id_(partition_id)
    , select_op_(std::move(select_op))
{
    hl_filter_ = select_op_.buildHighlight(SelectMode::GeometryVertex);
    if (hl_filter_) {
        hl_filter_->Update();
        highlight_data_->SetPartition(partition_id_, hl_filter_->GetOutput());
    }
}

GeometryVertexSelectorHighlight::~GeometryVertexSelectorHighlight()
{
    highlight_data_->SetPartition(partition_id_, nullptr);
}

void GeometryVertexSelectorHighlight::clear()
{
    disableHighlight();
    selections_.clear();
}

void GeometryVertexSelectorHighlight::disableHighlight()
{
    updateFilterAndNotify(hl_filter_, NCollection_List<IVtk_IdType>(), highlight_data_);
}

void GeometryVertexSelectorHighlight::enableHighlight()
{
    NCollection_List<IVtk_IdType> ids;
    for (const auto& [sid, gid] : selections_)
        ids.Append(sid);
    updateFilterAndNotify(hl_filter_, ids, highlight_data_);
}

GeometrySelectionVtk GeometryVertexSelectorHighlight::get() const
{
    GeometrySelectionVtk s;
    s.setType(ElementEnum::GeometryVertex);
    s.component_id = -1;
    for (const auto& [subId, geomId] : selections_)
        s.ids().push_back(geomId);
    return s;
}

void GeometryVertexSelectorHighlight::toggle(IVtk_IdType subId, Index geomId, SelectOp op)
{
    auto it = selections_.find(subId);
    const bool present = (it != selections_.end());
    switch (op) {
    case SelectOp::Append:
        if (!present) selections_.emplace(subId, geomId);
        break;
    case SelectOp::Remove:
        if (present) selections_.erase(it);
        break;
    case SelectOp::Toggle:
        if (present) selections_.erase(it);
        else selections_.emplace(subId, geomId);
        break;
    }

    enableHighlight();
}

void GeometryVertexSelectorHighlight::setupHighlightStyle(vtkActor& actor, vtkMapper& mapper)
{
    mountLineHighlight(actor, mapper);
}

// ─── Solid ─────────────────────────────────────────────

GeometrySolidSelectorHighlight::GeometrySolidSelectorHighlight(
    vtkPartitionedDataSet& highlight_data, unsigned int partition_id,
    GeometryActorSelectOp select_op)
    : highlight_data_(&highlight_data)
    , partition_id_(partition_id)
    , select_op_(std::move(select_op))
{
    hl_filter_ = select_op_.buildHighlight(SelectMode::GeometrySolid);
    if (hl_filter_) {
        hl_filter_->Update();
        highlight_data_->SetPartition(partition_id_, hl_filter_->GetOutput());
    }
}

GeometrySolidSelectorHighlight::~GeometrySolidSelectorHighlight()
{
    highlight_data_->SetPartition(partition_id_, nullptr);
}

void GeometrySolidSelectorHighlight::clear()
{
    disableHighlight();
    selections_.clear();
    highlighted_face_ids_.clear();
}

void GeometrySolidSelectorHighlight::disableHighlight()
{
    updateFilterAndNotify(hl_filter_, NCollection_List<IVtk_IdType>(), highlight_data_);
}

void GeometrySolidSelectorHighlight::enableHighlight()
{
    NCollection_List<IVtk_IdType> ids;
    for (const auto& fid : highlighted_face_ids_)
        ids.Append(fid);
    updateFilterAndNotify(hl_filter_, ids, highlight_data_);
}

GeometrySelectionVtk GeometrySolidSelectorHighlight::get() const
{
    GeometrySelectionVtk s;
    s.setType(ElementEnum::GeometrySolid);
    s.component_id = -1;
    for (const auto& [subId, geomId] : selections_)
        s.ids().push_back(geomId);
    return s;
}

void GeometrySolidSelectorHighlight::toggleSolid(GeomSolidId solidId, const std::vector<IVtk_IdType>& faceSubIds, SelectOp op)
{
    auto it = selections_.find(solidId);
    const bool was_present = (it != selections_.end());
    switch (op) {
    case SelectOp::Append:
        if (!was_present) selections_.emplace(solidId, solidId);
        break;
    case SelectOp::Remove:
        if (was_present) selections_.erase(it);
        break;
    case SelectOp::Toggle:
        if (was_present) selections_.erase(it);
        else selections_.emplace(solidId, solidId);
        break;
    }

    const bool now_present = (selections_.find(solidId) != selections_.end());
    if (now_present && !was_present) {
        for (IVtk_IdType faceSubId : faceSubIds)
            highlighted_face_ids_.insert(faceSubId);
    } else if (!now_present && was_present) {
        for (IVtk_IdType faceSubId : faceSubIds)
            highlighted_face_ids_.erase(faceSubId);
    }

    enableHighlight();
}

void GeometrySolidSelectorHighlight::setupHighlightStyle(vtkActor& actor, vtkMapper& mapper)
{
    mountLineHighlight(actor, mapper);
}
