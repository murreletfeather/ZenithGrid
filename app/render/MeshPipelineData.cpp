#include "MeshPipelineData.h"
#include "FaceHideFilter.h"
#include "MeshData.h"

#include <algorithm>
#include <atomic>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vtkAOSDataArrayTemplate.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkExtractCells.h>
#include <vtkExtractEdges.h>
#include <vtkExtractGeometry.h>
#include <vtkExtractPolyDataGeometry.h>
#include <vtkGeometryFilter.h>
#include <vtkIdTypeArray.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSMPTools.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>

namespace {
std::atomic_uint64_t next_mesh_generation { 0 };

/** @brief 只复制数据集包装；坐标、属性和连通性数组在发布后只读共享。 */
vtkSmartPointer<vtkPolyData> publishPolyData(vtkPolyData* input)
{
    auto output = vtkSmartPointer<vtkPolyData>::New();
    output->ShallowCopy(input);
    if (input->GetPoints()) {
        vtkNew<vtkPoints> points;
        points->SetData(input->GetPoints()->GetData());
        output->SetPoints(points);
    }
    auto wrap_cells = [](vtkCellArray* cells) {
        auto wrapped = vtkSmartPointer<vtkCellArray>::New();
        if (cells)
            wrapped->ShallowCopy(cells);
        return wrapped;
    };
    // 独立的单元包装拥有新的 MTime，避免隐藏恢复后的拾取和线框缓存复用旧状态。
    output->SetVerts(wrap_cells(input->GetVerts()));
    output->SetLines(wrap_cells(input->GetLines()));
    output->SetPolys(wrap_cells(input->GetPolys()));
    output->SetStrips(wrap_cells(input->GetStrips()));
    return output;
}

// 将组件局部点属性写入 PointData，供面/体 mapper 按点属性渲染。
void addPointAttributes(
    vtkPointData& point_data,
    const std::map<std::string, std::vector<double>>& attributes,
    vtkIdType point_count)
{
    if (point_count <= 0)
        return;

    const size_t num_points = static_cast<size_t>(point_count);
    for (const auto& [attr_name, attr_values] : attributes) {
        if (attr_values.empty())
            continue;

        // 属性数组按 [point0 分量..., point1 分量...] 存放，总长度必须是点数的整数倍。
        if (attr_values.size() % num_points != 0) {
            spdlog::error("Attribute {} size mismatch: {} values for {} points",
                attr_name, attr_values.size(), num_points);
            continue;
        }

        // PointData 是当前 VTK 数据对象自己的局部点数组，tuple 与局部点 id 一一对应。
        const size_t ncomp = attr_values.size() / num_points;
        auto array = vtkSmartPointer<vtkDoubleArray>::New();
        array->SetNumberOfComponents(static_cast<int>(ncomp));
        array->SetName(attr_name.c_str());
        array->SetNumberOfTuples(point_count);
        for (size_t i = 0; i < num_points; ++i)
            array->SetTuple(static_cast<vtkIdType>(i), &attr_values[i * ncomp]);
        point_data.AddArray(array);
    }
}

// 将边、面或体属性写入对应 VTK 数据对象的 CellData。
void addCellAttributes(
    vtkCellData& cell_data,
    const std::map<std::string, std::vector<double>>& attributes,
    vtkIdType cell_count,
    const char* cell_kind)
{
    if (cell_count <= 0)
        return;

    const size_t num_cells = static_cast<size_t>(cell_count);
    for (const auto& [attr_name, attr_values] : attributes) {
        // CellData 是当前 VTK 数据对象自己的局部 cell 数组，不需要全局偏移。
        if (attr_values.size() < num_cells) {
            spdlog::error("Attribute {} has insufficient values for {} cells", attr_name, cell_kind);
            continue;
        }
        if (attr_values.size() % num_cells != 0) {
            spdlog::error("Attribute {} size mismatch: {} values for {} {} cells",
                attr_name, attr_values.size(), num_cells, cell_kind);
            continue;
        }

        const size_t ncomp = attr_values.size() / num_cells;
        auto array = vtkSmartPointer<vtkDoubleArray>::New();
        array->SetNumberOfComponents(static_cast<int>(ncomp));
        array->SetName(attr_name.c_str());
        array->SetNumberOfTuples(cell_count);
        for (size_t i = 0; i < num_cells; ++i)
            array->SetTuple(static_cast<vtkIdType>(i), &attr_values[i * ncomp]);
        cell_data.AddArray(array);
    }
}

/** @brief 从自持连通性建立体网格，保留多面体的面与面位置索引。 */
void createSolidGrid(const MeshDataVtk& model_data, vtkPoints& points, vtkUnstructuredGrid& solid_data)
{
    // 以下 SetArray 的 save=1 禁止 VTK 释放外部数组；缓冲由快照中的源网格保活。
    // cells
    vtkNew<vtkCellArray> solid_cells;
    vtkNew<vtkAOSDataArrayTemplate<Index>> index_array;
    auto& vtk_indices = model_data.vtk_solid_cells_;
    index_array->SetArray(const_cast<Index*>(vtk_indices.data()), vtk_indices.size(), 1);

    vtkNew<vtkAOSDataArrayTemplate<Index>> offset_array;
    auto& vtk_offsets = model_data.vtk_solid_cells_offset_;
    offset_array->SetArray(const_cast<Index*>(vtk_offsets.data()), vtk_offsets.size(), 1);

    solid_cells->SetData(offset_array, index_array);

    // cell types
    vtkNew<vtkUnsignedCharArray> cell_types;
    auto& types = model_data.vtk_solid_cell_types_;
    cell_types->SetArray(const_cast<unsigned char*>(types.data()), types.size(), 1);

    // faces
    vtkNew<vtkCellArray> faces;
    vtkNew<vtkAOSDataArrayTemplate<Index>> faces_idx;
    auto& vtk_faces = model_data.vtk_solid_faces_;
    faces_idx->SetArray(const_cast<Index*>(vtk_faces.data()), vtk_faces.size(), 1);

    vtkNew<vtkAOSDataArrayTemplate<Index>> faces_offset;
    auto& vtk_faces_offset = model_data.vtk_solid_faces_offset_;
    faces_offset->SetArray(const_cast<Index*>(vtk_faces_offset.data()), vtk_faces_offset.size(), 1);

    faces->SetData(faces_offset, faces_idx);

    // face locations
    vtkNew<vtkCellArray> face_locations;
    vtkNew<vtkAOSDataArrayTemplate<Index>> face_loc_idx;
    auto& vtk_face_locations = model_data.vtk_solid_face_locations_;
    face_loc_idx->SetArray(const_cast<Index*>(vtk_face_locations.data()), vtk_face_locations.size(), 1);
    vtkNew<vtkAOSDataArrayTemplate<Index>> face_loc_offset;
    auto& vtk_face_locations_offset = model_data.vtk_solid_face_locations_offset_;
    face_loc_offset->SetArray(const_cast<Index*>(vtk_face_locations_offset.data()), vtk_face_locations_offset.size(), 1);
    face_locations->SetData(face_loc_offset, face_loc_idx);

    // solid ugrid
    solid_data.SetPoints(&points);
    solid_data.SetPolyhedralCells(cell_types, solid_cells, face_locations, faces);
}

}

MeshPipelineData::MeshPipelineData()
    : owner_thread_(std::this_thread::get_id())
{
}

MeshPipelineData::~MeshPipelineData() = default;

void MeshPipelineData::assertOwnerThread() const
{
    if (owner_thread_ != std::this_thread::get_id())
        throw std::logic_error("MeshPipelineData must be used on its creating thread");
}

void MeshPipelineData::loadModelData(const MeshDataVtk& data)
{
    assertOwnerThread();
    // MeshDataVtk 仅是引用视图，捕获一次自持缓冲；不对 VTK 数据做全量 DeepCopy。
    auto owned = std::make_shared<MeshData>();
    owned->vertex_positions_ = data.vertex_positions_;
    owned->solid_types_ = data.vtk_solid_cell_types_;
    owned->solid_vertices_ = data.vtk_solid_cells_;
    owned->solid_vertices_offset_ = data.vtk_solid_cells_offset_;
    owned->solid_faces_vertices_ = data.vtk_solid_faces_;
    owned->solid_faces_vertices_offset_ = data.vtk_solid_faces_offset_;
    owned->solid_faces_ = data.vtk_solid_face_locations_;
    owned->solid_faces_offset_ = data.vtk_solid_face_locations_offset_;
    owned->face_vertices_ = data.vtk_face_cells_;
    owned->face_vertices_offset_ = data.vtk_face_cells_offset_;
    owned->edge_vertices_ = data.vtk_edge_cells_;
    owned->vertex_attributes_ = data.vertex_attributes_;
    owned->edge_attributes_ = data.edge_attributes_;
    owned->face_attributes_ = data.face_attributes_;
    owned->solid_attributes_ = data.solid_attributes_;
    owned->vertex_count_ = static_cast<Index>(owned->vertex_positions_.size());
    auto blocks = data.model_blocks_ ? std::make_shared<BlockDatas>(*data.model_blocks_) : nullptr;
    source_ = owned;
    model_data_ = std::make_shared<MeshDataVtk>(MeshDataVtk {
        owned->solid_types_, owned->solid_vertices_, owned->solid_vertices_offset_,
        owned->solid_faces_vertices_, owned->solid_faces_vertices_offset_,
        owned->solid_faces_, owned->solid_faces_offset_,
        owned->face_vertices_, owned->face_vertices_offset_, owned->edge_vertices_,
        owned->vertex_positions_, owned->vertex_attributes_, owned->edge_attributes_,
        owned->face_attributes_, owned->solid_attributes_, blocks, data.component_id });
    face_data_ = vtkSmartPointer<vtkPolyData>::New();
    edge_data_ = vtkSmartPointer<vtkPolyData>::New();
    solid_data_ = vtkSmartPointer<vtkUnstructuredGrid>::New();
    vtkNew<vtkPoints> points_;
    vtkNew<vtkIdTypeArray> original_point_ids_;
    generation_ = ++next_mesh_generation;
    hidden_face_ids_.clear();
    dirty_ = true;
    // 本方法所有 SetArray 均使用 save=1：VTK 借用 vector 缓冲，不负责释放。
    // source_ 与已发布快照共同保活这些缓冲，不能把 save 改为 0。
    // 组件私有点集：坐标自持，连通性数组直接以局部点 id 作 VTK 点索引
    const auto& positions = model_data_->vertex_positions_;
    const vtkIdType point_count = static_cast<vtkIdType>(positions.size());
    vtkNew<vtkAOSDataArrayTemplate<double>> point_array;
    point_array->SetNumberOfComponents(3);
    if (point_count > 0)
        point_array->SetArray(const_cast<double*>(positions.front().data()), point_count * 3, 1);
    points_->SetData(point_array);

    // 拾取标签 vtkOriginalPointIds：存局部点 id，高亮提取直接作 VTK 点索引；
    // 与提取过滤器自动生成的同名数组同语义（均为局部点下标），裁剪链上被覆盖也不影响取值
    original_point_ids_->SetName("vtkOriginalPointIds");
    original_point_ids_->SetNumberOfComponents(1);
    original_point_ids_->SetNumberOfTuples(point_count);
    vtkSMPTools::For(0, point_count,
        [&](vtkIdType begin, vtkIdType end) {
            for (vtkIdType id = begin; id < end; ++id) {
                original_point_ids_->SetValue(id, static_cast<Index>(id));
            }
        });
    original_point_ids_->Modified();

    // face data
    vtkPolyData* face_poly = this->face_data_;
    {
        auto poly_data = vtkSmartPointer<vtkCellArray>::New();
        auto index_array = vtkSmartPointer<vtkAOSDataArrayTemplate<Index>>::New();
        auto& vtk_indices = this->model_data_->vtk_face_cells_;
        index_array->SetArray(const_cast<Index*>(vtk_indices.data()), vtk_indices.size(), 1);

        auto offset_array = vtkSmartPointer<vtkAOSDataArrayTemplate<Index>>::New();
        auto& vtk_offsets = this->model_data_->vtk_face_cells_offset_;
        offset_array->SetArray(const_cast<Index*>(vtk_offsets.data()), vtk_offsets.size(), 1);

        poly_data->SetData(offset_array, index_array);

        // face poly data
        face_poly->SetPoints(points_);
        face_poly->SetPolys(poly_data);

        face_poly->GetPointData()->AddArray(original_point_ids_.GetPointer());

        // 面索引数组：不裁剪时索引即模型面 id；随裁剪链透传，裁剪开启时拾取仍能映射回模型面
        vtkNew<vtkIdTypeArray> face_ids;
        face_ids->SetNumberOfComponents(1);
        face_ids->SetName("PrecessFaceIds");
        face_ids->SetNumberOfTuples(face_poly->GetNumberOfCells());
        vtkSMPTools::For(0, face_poly->GetNumberOfCells(),
            [&](vtkIdType begin, vtkIdType end) {
                for (vtkIdType id = begin; id < end; ++id) {
                    face_ids->SetValue(id, id);
                }
            });
        face_poly->GetCellData()->AddArray(face_ids);

        // 处理面属性
        addPointAttributes(*face_poly->GetPointData(), model_data_->vertex_attributes_, point_count);
        addCellAttributes(*face_poly->GetCellData(), model_data_->face_attributes_,
            face_poly->GetNumberOfCells(), "face");
    }

    // edge data
    vtkPolyData* edge_poly = this->edge_data_;
    {
        vtkNew<vtkCellArray> edge_cells;
        vtkNew<vtkAOSDataArrayTemplate<Index>> index_array;
        auto& vtk_indices = this->model_data_->vtk_edge_cells_;
        index_array->SetArray(const_cast<Index*>(vtk_indices.data()), vtk_indices.size(), 1);
        edge_cells->SetData(2, index_array);

        edge_poly->SetPoints(points_);
        edge_poly->SetLines(edge_cells);

        edge_poly->GetPointData()->AddArray(original_point_ids_.GetPointer());
        addCellAttributes(*edge_poly->GetCellData(), model_data_->edge_attributes_,
            edge_poly->GetNumberOfCells(), "edge");
    }

    // solid data
    vtkUnstructuredGrid* solid_ugird = this->solid_data_;
    createSolidGrid(*this->model_data_, *points_, *solid_ugird);
    vtkIdType solid_cells_count = solid_ugird->GetNumberOfCells();
    vtkNew<vtkIdTypeArray> originalCellIds;
    originalCellIds->SetNumberOfComponents(1);
    originalCellIds->SetName("vtkOriginalCellIds");
    originalCellIds->SetNumberOfTuples(solid_cells_count);
    // 使用并行方式设置原始单元ID
    vtkSMPTools::For(0, solid_cells_count,
        [&](vtkIdType begin, vtkIdType end) {
            for (vtkIdType cellId = begin; cellId < end; ++cellId) {
                originalCellIds->SetValue(cellId, cellId);
            }
        });
    solid_ugird->GetCellData()->AddArray(originalCellIds);
    solid_ugird->GetPointData()->AddArray(original_point_ids_.GetPointer());
    // 处理体属性
    addPointAttributes(*solid_ugird->GetPointData(), model_data_->vertex_attributes_, point_count);
    addCellAttributes(*solid_ugird->GetCellData(), model_data_->solid_attributes_, solid_cells_count, "solid");
}

void MeshPipelineData::setVisibility(bool visible)
{
    assertOwnerThread();
    visible_ = visible;
}

bool MeshPipelineData::isVisible() const
{
    assertOwnerThread();
    if (!visible_ || !source_)
        return false;
    if (!source_->edge_vertices_.empty())
        return true;
    for (auto type : source_->solid_types_) {
        if (!hidden_solid_types_.contains(type))
            return true;
    }
    const auto& offsets = source_->face_vertices_offset_;
    for (size_t id = 1; id < offsets.size(); ++id) {
        if (!hidden_face_types_.contains(MeshData::faceCellType(offsets[id] - offsets[id - 1]))
            && !std::binary_search(hidden_face_ids_.begin(), hidden_face_ids_.end(), static_cast<vtkIdType>(id - 1)))
            return true;
    }
    return false;
}

void MeshPipelineData::setCellTypeVisibility(int dimension, int type, bool visible)
{
    assertOwnerThread();
    if ((dimension != 2 && dimension != 3) || type < -1 || type > 255)
        return;
    auto& hidden = dimension == 2 ? hidden_face_types_ : hidden_solid_types_;
    const auto old_hidden = hidden;
    const auto set_type = [&](int cell_type) {
        if (visible)
            hidden.erase(cell_type);
        else
            hidden.insert(cell_type);
    };
    if (type >= 0)
        set_type(type);
    else if (source_) {
        if (dimension == 2) {
            const auto& offsets = source_->face_vertices_offset_;
            for (size_t id = 1; id < offsets.size(); ++id)
                set_type(MeshData::faceCellType(offsets[id] - offsets[id - 1]));
        } else {
            for (auto cell_type : source_->solid_types_)
                set_type(cell_type);
        }
    }
    dirty_ |= hidden != old_hidden;
}

void MeshPipelineData::showAllCellTypes()
{
    assertOwnerThread();
    dirty_ |= !hidden_face_types_.empty() || !hidden_solid_types_.empty();
    hidden_face_types_.clear();
    hidden_solid_types_.clear();
}

const std::set<int>& MeshPipelineData::hiddenCellTypes(int dimension) const
{
    return dimension == 2 ? hidden_face_types_ : hidden_solid_types_;
}

void MeshPipelineData::hideFaces(const std::vector<vtkIdType>& face_ids)
{
    assertOwnerThread();
    if (!face_data_)
        return;
    const auto count = face_data_->GetNumberOfCells();
    for (auto id : face_ids) {
        if (id >= 0 && id < count)
            hidden_face_ids_.push_back(id);
    }
    std::sort(hidden_face_ids_.begin(), hidden_face_ids_.end());
    hidden_face_ids_.erase(std::unique(hidden_face_ids_.begin(), hidden_face_ids_.end()), hidden_face_ids_.end());
    dirty_ = true;
}

void MeshPipelineData::showFaces(const std::vector<vtkIdType>& face_ids)
{
    assertOwnerThread();
    if (!face_data_)
        return;
    std::vector<vtkIdType> valid_ids;
    for (auto id : face_ids) {
        if (id >= 0 && id < face_data_->GetNumberOfCells())
            valid_ids.push_back(id);
    }
    if (valid_ids.empty())
        return;
    if (!visible_) {
        isolateFaces(valid_ids);
        return;
    }
    std::sort(valid_ids.begin(), valid_ids.end());
    std::erase_if(hidden_face_ids_, [&valid_ids](auto id) {
        return std::binary_search(valid_ids.begin(), valid_ids.end(), id);
    });
    dirty_ = true;
}

void MeshPipelineData::isolateFaces(const std::vector<vtkIdType>& face_ids)
{
    assertOwnerThread();
    if (!face_data_)
        return;
    std::vector<bool> shown(static_cast<std::size_t>(face_data_->GetNumberOfCells()), false);
    for (auto id : face_ids) {
        if (id >= 0 && id < static_cast<vtkIdType>(shown.size()))
            shown[static_cast<std::size_t>(id)] = true;
    }
    hidden_face_ids_.clear();
    for (vtkIdType id = 0; id < static_cast<vtkIdType>(shown.size()); ++id) {
        if (!shown[static_cast<std::size_t>(id)])
            hidden_face_ids_.push_back(id);
    }
    visible_ = true;
    dirty_ = true;
}

void MeshPipelineData::showAllFaces()
{
    assertOwnerThread();
    if (!hidden_face_ids_.empty()) {
        hidden_face_ids_.clear();
        dirty_ = true;
    }
}

bool MeshPipelineData::reverseVisibility()
{
    assertOwnerThread();
    if (!visible_) {
        showAllFaces();
        showAllCellTypes();
        visible_ = true;
    } else if (!source_ || (hidden_face_ids_.empty() && hidden_face_types_.empty() && hidden_solid_types_.empty())) {
        visible_ = false;
    } else {
        // 面的有效隐藏是类型过滤与人工隐藏的并集，反转必须对这个并集取补集。
        // 完全隐藏的类型用类型开关表达，其余仅保留局部隐藏 ID，避免两层状态重叠。
        if (!hidden_face_ids_.empty() || !hidden_face_types_.empty()) {
            std::vector<vtkIdType> reversed;
            std::set<int> all_types, visible_types;
            const auto& offsets = source_->face_vertices_offset_;
            for (size_t id = 1; id < offsets.size(); ++id) {
                const int type = MeshData::faceCellType(offsets[id] - offsets[id - 1]);
                const auto face_id = static_cast<vtkIdType>(id - 1);
                all_types.insert(type);
                if (hidden_face_types_.contains(type)
                    || std::binary_search(hidden_face_ids_.begin(), hidden_face_ids_.end(), face_id))
                    visible_types.insert(type);
                else
                    reversed.push_back(face_id);
            }
            hidden_face_types_ = std::move(all_types);
            for (int type : visible_types)
                hidden_face_types_.erase(type);
            std::erase_if(reversed, [&](vtkIdType id) {
                return hidden_face_types_.contains(MeshData::faceCellType(offsets[id + 1] - offsets[id]));
            });
            hidden_face_ids_ = std::move(reversed);
        }
        // 体类型只在已有局部体过滤时反转，沿用局部面操作不改变其他维度的规则。
        if (!hidden_solid_types_.empty()) {
            std::set<int> reversed;
            for (auto type : source_->solid_types_) {
                if (!hidden_solid_types_.contains(type))
                    reversed.insert(type);
            }
            hidden_solid_types_ = std::move(reversed);
        }
        dirty_ = true;
    }
    return visible_;
}

void MeshPipelineData::setClipPlane(vtkPlane* plane)
{
    assertOwnerThread();
    MeshClipPlane parameters;
    parameters.enabled = plane != nullptr;
    if (plane) {
        plane->GetOrigin(parameters.origin.data());
        plane->GetNormal(parameters.normal.data());
    }
    setClipPlane(parameters);
}

void MeshPipelineData::setClipPlane(const MeshClipPlane& plane)
{
    assertOwnerThread();
    // 控件可能重复发布同一平面，避免重算不变的裁剪输出。
    if (clip_.enabled == plane.enabled && clip_.origin == plane.origin && clip_.normal == plane.normal)
        return;
    clip_ = plane;
    dirty_ = true;
}

void MeshPipelineData::rebuildOutputs()
{
    // 每次重建使用新的 Filter，下一次 Update 不会改写已发布快照引用的输出数组。
    vtkNew<FaceHideFilter> hider;
    vtkNew<vtkIdTypeArray> hidden_ids;
    for (auto id : hidden_face_ids_)
        hidden_ids->InsertNextValue(id);
    // 类型隐藏与人工面隐藏都使用原始面 ID，避免过滤后的局部下标混入持久状态。
    const auto& face_offsets = source_->face_vertices_offset_;
    if (!hidden_face_types_.empty()) {
        for (size_t id = 1; id < face_offsets.size(); ++id) {
            if (hidden_face_types_.contains(MeshData::faceCellType(face_offsets[id] - face_offsets[id - 1])))
                hidden_ids->InsertNextValue(static_cast<vtkIdType>(id - 1));
        }
    }
    hider->SetInputData(face_data_);
    hider->SetHiddenCellIds(hidden_ids);
    hider->Update();

    vtkNew<vtkPlane> plane;
    plane->SetOrigin(clip_.origin.data());
    plane->SetNormal(clip_.normal.data());
    vtkNew<vtkExtractPolyDataGeometry> face_clipper;
    vtkNew<vtkExtractPolyDataGeometry> edge_clipper;
    vtkNew<vtkExtractGeometry> solid_clipper;
    vtkPolyData* faces = hider->GetOutput();
    vtkPolyData* edges = edge_data_;
    vtkUnstructuredGrid* solids = solid_data_;
    // 在提取外表面前过滤体单元，让保留单元原先被遮住的交界面重新暴露。
    vtkNew<vtkExtractCells> type_filter;
    if (!hidden_solid_types_.empty()) {
        std::vector<vtkIdType> kept_ids;
        for (size_t id = 0; id < source_->solid_types_.size(); ++id) {
            if (!hidden_solid_types_.contains(source_->solid_types_[id]))
                kept_ids.push_back(static_cast<vtkIdType>(id));
        }
        type_filter->SetInputData(solids);
        type_filter->SetCellIds(kept_ids.data(), static_cast<vtkIdType>(kept_ids.size()));
        type_filter->AssumeSortedAndUniqueIdsOn();
        // 原始体 ID 已在输入携带，不允许过滤器用重排后的下标覆盖。
        type_filter->PassThroughCellIdsOff();
        type_filter->Update();
        solids = type_filter->GetOutput();
    }
    vtkUnstructuredGrid* typed_solids = solids;
    if (clip_.enabled) {
        face_clipper->SetInputData(faces);
        face_clipper->SetImplicitFunction(plane);
        face_clipper->Update();
        faces = face_clipper->GetOutput();
        edge_clipper->SetInputData(edges);
        edge_clipper->SetImplicitFunction(plane);
        edge_clipper->Update();
        edges = edge_clipper->GetOutput();
        solid_clipper->SetInputData(solids);
        solid_clipper->SetImplicitFunction(plane);
        solid_clipper->Update();
        solids = solid_clipper->GetOutput();
    }
    vtkNew<vtkGeometryFilter> surface;
    surface->SetInputData(solids);
    surface->Update();
    // 内部线框按体类型过滤，仍保持不受裁剪及人工面隐藏影响的既有语义。
    // 复用已发布的只读输出；仅源数据或体类型过滤改变时重新提边。
    vtkSmartPointer<vtkPolyData> internal_edges;
    if (prepared_ && prepared_->source == source_ && prepared_->hidden_solid_types == hidden_solid_types_) {
        internal_edges = prepared_->solid_internal_edges;
    } else {
        vtkNew<vtkExtractEdges> extractor;
        extractor->SetInputData(typed_solids);
        // 保留输入点集及身份数组，不按坐标合并；拾取时经身份数组还原组件点 ID。
        extractor->UseAllPointsOn();
        extractor->Update();
        internal_edges = publishPolyData(extractor->GetOutput());
    }

    prepared_ = std::make_shared<MeshDataSnapshot>();
    prepared_->source = source_;
    prepared_->hidden_face_types = hidden_face_types_;
    prepared_->hidden_solid_types = hidden_solid_types_;
    prepared_->model_data = model_data_;
    prepared_->original_faces = publishPolyData(face_data_);
    prepared_->original_edges = publishPolyData(edge_data_);
    prepared_->original_solids = vtkSmartPointer<vtkUnstructuredGrid>::New();
    prepared_->original_solids->ShallowCopy(solid_data_);
    prepared_->visible_faces = publishPolyData(faces);
    prepared_->visible_edges = publishPolyData(edges);
    prepared_->solid_surface = publishPolyData(surface->GetOutput());
    prepared_->solid_internal_edges = internal_edges;
    dirty_ = false;
}

std::shared_ptr<const MeshDataSnapshot> MeshPipelineData::createSnapshot()
{
    assertOwnerThread();
    if (!source_)
        throw std::logic_error("MeshPipelineData has no source mesh");
    if (dirty_ || !prepared_)
        rebuildOutputs();
    auto snapshot = std::make_shared<MeshDataSnapshot>(*prepared_);
    snapshot->generation = generation_;
    snapshot->revision = ++revision_;
    snapshot->visible = visible_;
    snapshot->clip = clip_;
    return snapshot;
}
