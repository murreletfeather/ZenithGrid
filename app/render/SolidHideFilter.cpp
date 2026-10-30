#include "SolidHideFilter.h"

#include <spdlog/spdlog.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataSetAttributes.h>
#include <vtkGenericCell.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>

#include <vector>

vtkStandardNewMacro(SolidHideFilter);
vtkCxxSetObjectMacro(SolidHideFilter, HiddenCellIds, vtkIdTypeArray);

SolidHideFilter::SolidHideFilter() = default;

SolidHideFilter::~SolidHideFilter()
{
    this->SetHiddenCellIds(nullptr);
}

void SolidHideFilter::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "HiddenCellIds: " << this->HiddenCellIds << "\n";
}

namespace {

//! @brief 用新对象包裹输入数据集的连通性包装，使输出拥有独立 MTime。
//! 空隐藏集透传时若直接共享 cell array 对象，隐藏→显示切换后下游可能因
//! MTime 未变化而复用旧缓存（与 FaceHideFilter 同源的拾取缓存问题）。
//! @return 包装后的连通性；cells 为空时返回 nullptr，调用方必须先判空。
vtkSmartPointer<vtkCellArray> wrapCells(vtkCellArray* cells)
{
    if (!cells)
        return nullptr;
    auto wrapped = vtkSmartPointer<vtkCellArray>::New();
    wrapped->ShallowCopy(cells);
    return wrapped;
}

//! @brief 透传完整输入数据：共享点坐标与点属性，连通性另建包装以保证独立 MTime。
void emitPassthrough(vtkUnstructuredGrid* output, vtkUnstructuredGrid* input)
{
    output->SetPoints(input->GetPoints());
    output->GetPointData()->ShallowCopy(input->GetPointData());
    output->GetCellData()->ShallowCopy(input->GetCellData());

    auto cells = wrapCells(input->GetCells());
    auto faces = wrapCells(input->GetPolyhedronFaces());
    auto face_locations = wrapCells(input->GetPolyhedronFaceLocations());
    // 透传同样先判空：没有连通性就没有可输出的单元。
    if (!cells)
        return;
    if (faces && face_locations) {
        output->SetPolyhedralCells(input->GetCellTypes(), cells, face_locations, faces);
    } else {
        // 多面体的 faces 与 faceLocations 必须成对存在，只有其一时面拓扑不完整，
        // 降级为普通单元输出并留下日志便于定位异常输入。
        if (faces || face_locations)
            spdlog::warn("SolidHideFilter 透传：多面体面拓扑不完整，降级为普通单元输出");
        output->SetCells(input->GetCellTypes(), cells);
    }
}

} // namespace

int SolidHideFilter::RequestData(
    vtkInformation*,
    vtkInformationVector** input_vector,
    vtkInformationVector* output_vector)
{
    auto* input = vtkUnstructuredGrid::GetData(input_vector[0]);
    auto* output = vtkUnstructuredGrid::GetData(output_vector);
    if (!input || !output) {
        spdlog::error("SolidHideFilter::RequestData 输入或输出数据为空：输入有效={}，输出有效={}",
            input != nullptr, output != nullptr);
        return 0;
    }

    const vtkIdType cell_count = input->GetNumberOfCells();
    if (cell_count == 0) {
        output->Initialize();
        return 1;
    }

    // 空隐藏集（含未设置）透传完整数据。
    const vtkIdType hidden_count = this->HiddenCellIds ? this->HiddenCellIds->GetNumberOfValues() : 0;
    if (hidden_count == 0) {
        emitPassthrough(output, input);
        return 1;
    }

    // 标记待移除单元，越界 id 忽略。
    std::vector<char> removed(static_cast<size_t>(cell_count), 0);
    vtkIdType hidden_valid_count = 0;
    for (vtkIdType i = 0; i < hidden_count; ++i) {
        const vtkIdType id = this->HiddenCellIds->GetValue(i);
        if (id >= 0 && id < cell_count) {
            if (!removed[static_cast<size_t>(id)])
                ++hidden_valid_count;
            removed[static_cast<size_t>(id)] = 1;
        }
    }
    if (hidden_valid_count == 0) {
        // 没有任何有效隐藏 id：退化为透传，避免留下与输入重复的完整重建。
        emitPassthrough(output, input);
        return 1;
    }

    // kept_count 为真实保留单元数；全部隐藏时得到空输出。
    const vtkIdType kept_count = cell_count - hidden_valid_count;

    // 点集不压缩：输出共享输入点坐标与点属性，仅重建单元连通性。
    // 未被保留单元引用的点仍留在输出中，下游 vtkGeometryFilter/vtkExtractEdges 只读不写。
    output->SetPoints(input->GetPoints());
    output->GetPointData()->ShallowCopy(input->GetPointData());

    // COPYTUPLE 只做整元组拷贝，输出数组按输入数组同类型创建（整型不提升为浮点）。
    vtkCellData* in_cell_data = input->GetCellData();
    vtkCellData* out_cell_data = output->GetCellData();
    out_cell_data->CopyAllocate(in_cell_data, kept_count, 0, vtkDataSetAttributes::COPYTUPLE);

    // 多面体单元只写连通性会丢掉面拓扑，必须同时重建 faces 与 faceLocations，
    // 因此先按保留单元重建三套数组，最后一次性交给 SetCells/SetPolyhedralCells。
    vtkNew<vtkUnsignedCharArray> out_types;
    vtkNew<vtkCellArray> out_cells;
    vtkNew<vtkCellArray> out_faces;
    vtkNew<vtkCellArray> out_face_locations;
    vtkNew<vtkGenericCell> polyhedron;
    vtkNew<vtkIdList> face_ids;
    vtkNew<vtkIdList> stream;
    bool has_polyhedra = false;
    vtkIdType out_cell_id = 0;
    for (vtkIdType cell_id = 0; cell_id < cell_count; ++cell_id) {
        if (removed[static_cast<size_t>(cell_id)])
            continue;
        const int type = input->GetCellType(cell_id);
        if (type == VTK_POLYHEDRON) {
            has_polyhedra = true;
            input->GetCell(cell_id, polyhedron);
            out_cells->InsertNextCell(polyhedron->GetPointIds());
            // 每个面的点 id 追加到 faces，并记录本单元拥有的新面 id。
            face_ids->Initialize();
            const vtkIdType face_count = polyhedron->GetNumberOfFaces();
            for (vtkIdType face = 0; face < face_count; ++face) {
                out_faces->InsertNextCell(polyhedron->GetFace(face)->GetPointIds());
                face_ids->InsertNextId(out_faces->GetNumberOfCells() - 1);
            }
            out_face_locations->InsertNextCell(face_ids);
        } else {
            // GetFaceStream 对普通单元返回普通点列表。
            input->GetFaceStream(cell_id, stream);
            out_cells->InsertNextCell(stream);
            // faceLocations 按单元索引，普通单元一律用空条目占位，
            // 保证多面体单元的面 id 列表与单元下标对齐。
            out_face_locations->InsertNextCell(0, nullptr);
        }
        out_types->InsertNextValue(static_cast<unsigned char>(type));
        out_cell_data->CopyData(in_cell_data, cell_id, out_cell_id);
        ++out_cell_id;
    }

    if (has_polyhedra)
        output->SetPolyhedralCells(out_types, out_cells, out_face_locations, out_faces);
    else
        output->SetCells(out_types, out_cells);

    return 1;
}
