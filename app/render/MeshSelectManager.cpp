#include "MeshSelectManager.h"
#include "MeshActorManagerSelectOp.h"
#include "MeshAreaPick.h"
#include "Selection.h"
#include "SelectorHighlight.h"
#include <vtkActor.h>
#include <vtkCellData.h>
#include <vtkCompositePolyDataMapper.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkHardwarePicker.h>
#include <vtkIdTypeArray.h>
#include <vtkMatrix4x4.h>
#include <vtkPartitionedDataSet.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPointPicker.h>
#include <vtkPolyData.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>

MeshSelectManager::MeshSelectManager(vtkRenderer& renderer, vtkActor& highlight_actor, MeshActorManagerSelectOp& op)
    : op_(&op)
    , renderer_(&renderer)
    , highlight_actor_(&highlight_actor)
{
    component_picker_ = vtkSmartPointer<vtkHardwarePicker>::New();
    highlight_mapper_ = vtkSmartPointer<vtkCompositePolyDataMapper>::New();
    highlight_data_ = vtkSmartPointer<vtkPartitionedDataSet>::New();

    highlight_mapper_->SetInputDataObject(highlight_data_);
    component_picker_->PickFromListOn();
    op_->observePickList(component_picker_->GetPickList());

#ifdef __EMSCRIPTEN__
    // GLES3/WebGL2 未实现 glPointSize，硬件点拾取不正确，改用纯软件拾取的 vtkPointPicker
    vertex_picker_ = vtkSmartPointer<vtkPointPicker>::New();
    vertex_picker_->SetTolerance(0.004);
    vertex_picker_->PickFromListOn();
#else
    // 顶点吸附专用拾取器：与组件拾取分离（SnapToMeshPoint 会改变拾取语义）
    vertex_picker_ = vtkSmartPointer<vtkHardwarePicker>::New();
    vertex_picker_->SnapToMeshPointOn();
    vertex_picker_->SetPixelTolerance(5);
    vertex_picker_->PickFromListOn();
#endif
    op_->observePickList(vertex_picker_->GetPickList());
}

void MeshSelectManager::select(double posx, double posy, SelectOp op)
{
    if (this->select_mode_ == SelectMode::None)
        return;

#ifdef __EMSCRIPTEN__
    if (this->select_mode_ == SelectMode::Vertex) {
        vertex_picker_->Pick(posx, posy, 0, renderer_);
        vtkActor* picked_actor = vertex_picker_->GetActor();
        auto component_id = op_->getComponentId(picked_actor);
        if (!picked_actor || !component_id) {
            if (op == SelectOp::Toggle)
                this->clearSelection();
            return;
        }
        if (auto* sel = dynamic_cast<VertexSelectorHighlight*>(getOrCreateSelector(*component_id)))
            sel->selectPickedPoint(vertex_picker_->GetDataSet(), vertex_picker_->GetPointId());
        return;
    }
#endif

    if (this->select_mode_ == SelectMode::Vertex) {
        component_picker_->SnapToMeshPointOn();
        component_picker_->SetPixelTolerance(5);
    } else {
        component_picker_->SnapToMeshPointOff();
    }

    component_picker_->Pick(posx, posy, 0, renderer_);

    vtkActor* picked_actor = component_picker_->GetActor();
    auto component_id = op_->getComponentId(picked_actor);
    if (!picked_actor || !component_id) {
        // 点选落空：仅 Toggle 模式清空全部选择；Append/Remove 模式点空白不做事
        if (op == SelectOp::Toggle)
            this->clearSelection();
        return;
    }

    if (auto* sel = getOrCreateSelector(*component_id))
        sel->select(posx, posy, component_picker_.GetPointer(), picked_actor,
            component_picker_->GetCellId(), component_picker_->GetPointId(), op);
}

void MeshSelectManager::selectArea(int xmin, int ymin, int xmax, int ymax, SelectOp op)
{
    if (this->select_mode_ == SelectMode::None)
        return;

    // 一次拖框只构造一次视锥（穿透路径面/边/体共用；非穿透不建）。
    vtkSmartPointer<vtkPlanes> frustum;
    if (penetrating_) {
        frustum = area_pick::buildSelectionFrustum(renderer_, xmin, ymin, xmax, ymax);
        if (!frustum)
            return;
    }

    // 按模式一次性收集候选 actor（所有可见组件一起拾取，互为 z-buffer）
    std::vector<vtkActor*> targets;
    auto add_if_actor = [&](vtkProp* prop) {
        if (auto* actor = vtkActor::SafeDownCast(prop))
            targets.push_back(actor);
    };
    const bool actor_pick = !penetrating_
        || select_mode_ == SelectMode::Face
        || select_mode_ == SelectMode::Edge;
    if (actor_pick) {
        for (Index comp_id : op_->getAllComponentIds()) {
            auto select_op = op_->getSelectOp(comp_id);
            if (!select_op || !select_op->isVisible())
                continue;
            if (!penetrating_) {
                switch (select_mode_) {
                case SelectMode::Face:
                    add_if_actor(&select_op->getFaceActor());
                    break;
                case SelectMode::Solid:
                    add_if_actor(&select_op->getSolidActor());
                    break;
                case SelectMode::Edge:
                case SelectMode::Vertex:
                    add_if_actor(&select_op->getFaceActor());
                    add_if_actor(&select_op->getEdgeActor());
                    add_if_actor(&select_op->getSolidActor());
                    break;
                default:
                    break;
                }
            } else {
                // 穿透且面/边：只用显式面/独立边 actor，不带 solid actor（含内部线框）。
                add_if_actor(&select_op->getFaceActor());
                if (select_mode_ == SelectMode::Edge)
                    add_if_actor(&select_op->getEdgeActor());
            }
        }
        if (targets.empty())
            return;
    }

    // 一次 actor 拾取（非穿透硬件 / 穿透面边视锥），结果供逐组件分发；点/体穿透为空。
    auto hits = actor_pick
        ? (penetrating_
            ? area_pick::executePenetratingAreaPicks(renderer_, targets, frustum, xmin, ymin, xmax, ymax)
            : area_pick::executeAreaPicks(renderer_, targets,
                xmin, ymin, xmax, ymax, vtkDataObject::FIELD_ASSOCIATION_CELLS))
        : std::unordered_map<vtkProp*, std::unordered_set<vtkIdType>> {};

    // 逐组件分发。非穿透与穿透面/边走通用 selectArea；点/体穿透各自算好 ID 后走具体入口。
    // 点/体用组件（solid actor）的变换矩阵，面/边在各自 actor 数据与拾取里已带变换。
    for (Index comp_id : op_->getAllComponentIds()) {
        auto select_op = op_->getSelectOp(comp_id);
        if (!select_op || !select_op->isVisible())
            continue;
        auto* sel = getOrCreateSelector(comp_id);
        if (!sel)
            continue;

        if (!penetrating_ || select_mode_ == SelectMode::Face || select_mode_ == SelectMode::Edge) {
            sel->selectArea(hits, xmin, ymin, xmax, ymax, op);
            continue;
        }

        auto* solid_actor = vtkActor::SafeDownCast(&select_op->getSolidActor());
        vtkMatrix4x4* component_matrix = solid_actor ? solid_actor->GetMatrix() : nullptr;
        if (select_mode_ == SelectMode::Vertex) {
            // 直接遍历组件公共点表：先查可选位图，再用视锥六面判断（含近远平面）。
            auto* vsel = dynamic_cast<VertexSelectorHighlight*>(sel);
            auto* points = vsel ? select_op->getPoints() : nullptr;
            if (!points)
                continue;
            auto* mask = select_op->getSelectablePoints();
            std::unordered_set<vtkIdType> point_ids;
            const vtkIdType n = points->GetNumberOfPoints();
            for (vtkIdType id = 0; id < n; ++id) {
                if (mask && (id >= static_cast<vtkIdType>(mask->size())
                        || (*mask)[static_cast<size_t>(id)] == 0))
                    continue;
                double p[3];
                points->GetPoint(id, p);
                if (area_pick::isPointInFrustum(frustum, component_matrix, p[0], p[1], p[2]))
                    point_ids.insert(id);
            }
            vsel->selectPenetratingPoints(point_ids, op);
        } else if (select_mode_ == SelectMode::Solid) {
            // 直接对裁剪后体网格求交，cell 下标经 vtkOriginalCellIds 还原为原始体 id。
            auto* ssel = dynamic_cast<SolidSelectorHighlight*>(sel);
            auto* vol = ssel ? select_op->getClippedSolidData() : nullptr;
            if (!vol)
                continue;
            auto picked = area_pick::penetratingPickCells(frustum, vol, component_matrix);
            auto* orig = vtkIdTypeArray::SafeDownCast(
                vol->GetCellData()->GetArray("vtkOriginalCellIds"));
            std::unordered_set<vtkIdType> solid_ids;
            // 缺 vtkOriginalCellIds 时不猜测映射：跳过该组件的全部命中（合法空结果）。
            if (orig) {
                for (vtkIdType c : picked) {
                    if (c >= 0 && c < orig->GetNumberOfTuples()) {
                        const vtkIdType o = orig->GetValue(c);
                        if (o >= 0)
                            solid_ids.insert(o);
                    }
                }
            }
            ssel->selectPenetratingSolids(solid_ids, op);
        }
    }
}

void MeshSelectManager::setPenetrating(bool enabled)
{
    penetrating_ = enabled;
}

void MeshSelectManager::setSelectMode(SelectMode select_mode)
{
    this->select_mode_ = select_mode;
    this->component_selectors_.clear();

    highlight_data_->Initialize();
    applyHighlightStyle(select_mode);
}

void MeshSelectManager::setFaceSelectionByAngle(bool enabled, double angle_deg)
{
    face_selection_spread_options_.enabled = enabled;
    face_selection_spread_options_.angle_deg = angle_deg;

    // 每个 Component 拥有独立选择器，需要同步更新已经创建的面选择器。
    for (auto& component_selector : component_selectors_) {
        if (auto* face_selector = dynamic_cast<FaceSelectorHighlight*>(component_selector.second.get()))
            face_selector->setSpreadOptions(face_selection_spread_options_);
    }
}

void MeshSelectManager::clearSelection()
{
    this->component_selectors_.clear();
}

void MeshSelectManager::setMeshIdQuery(const IMeshIdQuery* id_query)
{
    id_query_ = id_query;
}

std::optional<MeshVertexSnap> MeshSelectManager::snapMeshVertex(double posx, double posy)
{
    vertex_picker_->Pick(posx, posy, 0, renderer_);
    const vtkIdType picked_point_id = vertex_picker_->GetPointId();
    if (picked_point_id == -1)
        return std::nullopt;

    vtkDataSet* data_set = vertex_picker_->GetDataSet();
    vtkActor* picked_actor = vertex_picker_->GetActor();
    if (!data_set || !picked_actor)
        return std::nullopt;

    auto component_id = op_->getComponentId(picked_actor);
    if (!component_id)
        return std::nullopt;

    // 局部点 id：vtkOriginalPointIds 与数据集点下标同语义（裁剪链上被覆盖亦成立）
    Index local_id = -1;
    if (auto ids = vtkIdTypeArray::SafeDownCast(
            data_set->GetPointData()->GetArray("vtkOriginalPointIds")))
        local_id = static_cast<Index>(ids->GetValue(picked_point_id));

    // 位置取数据集存储的精确坐标：同一顶点跨次拾取位级一致，判同一点可靠
    double p[3];
    data_set->GetPoint(picked_point_id, p);

    return MeshVertexSnap { *component_id, local_id, { p[0], p[1], p[2] } };
}

std::unique_ptr<Selection> MeshSelectManager::getSelection()
{
    auto result = std::make_unique<Selection>();

    for (auto& [comp_id, sel] : component_selectors_) {
        auto selection = sel->get();

        for (const auto& id : selection.ids)
            result->ids.push_back(id);

        if (selection.ids.size()) {
            result->component_id = comp_id;
            result->type = selection.type;
        }
    }

    return result;
}

std::unordered_map<Index, std::vector<vtkIdType>> MeshSelectManager::selectedFacesByComponent()
{
    std::unordered_map<Index, std::vector<vtkIdType>> result;
    if (select_mode_ != SelectMode::Face)
        return result;

    for (auto& [comp_id, selector] : component_selectors_) {
        auto* face_selector = dynamic_cast<FaceSelectorHighlight*>(selector.get());
        if (!face_selector)
            continue;

        SelectionVtk selection = face_selector->get();
        if (selection.ids.empty())
            continue;

        auto& face_ids = result[comp_id];
        face_ids.reserve(selection.ids.size());
        for (Index id : selection.ids)
            face_ids.push_back(static_cast<vtkIdType>(id));
    }
    return result;
}

SelectorHighlight* MeshSelectManager::getOrCreateSelector(Index component_id)
{
    auto it = component_selectors_.find(component_id);
    if (it != component_selectors_.end())
        return it->second.get();

    auto select_op = op_->getSelectOp(component_id);
    if (!select_op)
        return nullptr;

    unsigned int pid = component_selectors_.size();
    std::unique_ptr<SelectorHighlight> sel;
    switch (select_mode_) {
    case SelectMode::Face: {
        auto face_selector = std::make_unique<FaceSelectorHighlight>(
            *renderer_, *highlight_data_, pid, std::move(*select_op));
        face_selector->setSpreadOptions(face_selection_spread_options_);
        sel = std::move(face_selector);
        break;
    }
    case SelectMode::Edge:
        sel = std::make_unique<EdgeSelectorHighlight>(*renderer_, *highlight_data_, pid, std::move(*select_op),
            component_id, id_query_);
        break;
    case SelectMode::Solid:
        sel = std::make_unique<SolidSelectorHighlight>(*renderer_, *highlight_data_, pid, std::move(*select_op));
        break;
    case SelectMode::Vertex:
        sel = std::make_unique<VertexSelectorHighlight>(*renderer_, *highlight_data_, pid, std::move(*select_op),
            component_id, id_query_);
        break;
    default:
        return nullptr;
    }

    auto* ptr = sel.get();
    component_selectors_[component_id] = std::move(sel);
    return ptr;
}

void MeshSelectManager::applyHighlightStyle(SelectMode mode)
{
    switch (mode) {
    case SelectMode::Face:
        FaceSelectorHighlight::setupHighlightStyle(*highlight_actor_, *highlight_mapper_);
        break;
    case SelectMode::Edge:
        EdgeSelectorHighlight::setupHighlightStyle(*highlight_actor_, *highlight_mapper_);
        break;
    case SelectMode::Solid:
        SolidSelectorHighlight::setupHighlightStyle(*highlight_actor_, *highlight_mapper_);
        break;
    case SelectMode::Vertex:
        VertexSelectorHighlight::setupHighlightStyle(*highlight_actor_, *highlight_mapper_);
        break;
    }
}

void MeshSelectManager::setHighlightVisible(bool visible)
{
    if (highlight_visible_ == visible)
        return;
    highlight_visible_ = visible;
    highlight_mapper_->SetInputDataObject(visible ? highlight_data_.Get() : nullptr);
}

void MeshSelectManager::setHighlightVisible(Index component_id, bool visible)
{
    auto it = component_selectors_.find(component_id);
    if (it == component_selectors_.end())
        return;
    if (visible)
        it->second->enableHighlight();
    else
        it->second->disableHighlight();
}
