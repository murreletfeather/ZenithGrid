#include "MakeMeshDataVtk.h"
#include "MeshData.h"
#include "MeshPipelineData.h"
#include "SolidHideFilter.h"

#include <catch2/catch_test_macros.hpp>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkGenericCell.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPlane.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>

#include <algorithm>
#include <vector>
namespace {

//! @brief 读取体身份数组，同时断言类型仍是 vtkIdTypeArray（拾取侧按原类型读取）。
std::vector<vtkIdType> solidIdValues(vtkDataSet* data)
{
    auto* ids = vtkIdTypeArray::SafeDownCast(data->GetCellData()->GetArray("vtkOriginalCellIds"));
    REQUIRE(ids != nullptr);
    std::vector<vtkIdType> values(static_cast<size_t>(ids->GetNumberOfTuples()));
    for (vtkIdType i = 0; i < ids->GetNumberOfTuples(); ++i)
        values[static_cast<size_t>(i)] = ids->GetValue(i);
    return values;
}

//! @brief 表面片按体聚合后去重排序，用于比较幸存体集合而不是面片数量。
std::vector<vtkIdType> uniqueSolidIds(vtkPolyData* surface)
{
    auto values = solidIdValues(surface);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

//! @brief 从体集合中剔除指定体，用于断言“身份不被压缩”。
std::vector<vtkIdType> withoutIds(std::vector<vtkIdType> base, const std::vector<vtkIdType>& removed)
{
    for (auto id : removed)
        base.erase(std::remove(base.begin(), base.end(), id), base.end());
    return base;
}

//! @brief 构造 size 个独立六面体的体网格，单元属性携带原始体 ID，便于校验一一对应。
vtkSmartPointer<vtkUnstructuredGrid> makeHexGrid(vtkIdType size)
{
    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> cells;
    vtkNew<vtkUnsignedCharArray> types;
    vtkNew<vtkIdTypeArray> original_ids;
    original_ids->SetName("vtkOriginalCellIds");
    vtkNew<vtkDoubleArray> weights;
    weights->SetName("solidWeight");

    for (vtkIdType cell_id = 0; cell_id < size; ++cell_id) {
        const double x = 2.0 * static_cast<double>(cell_id);
        const double corners[8][3] = {
            { x, 0.0, 0.0 }, { x + 1.0, 0.0, 0.0 }, { x + 1.0, 1.0, 0.0 }, { x, 1.0, 0.0 },
            { x, 0.0, 1.0 }, { x + 1.0, 0.0, 1.0 }, { x + 1.0, 1.0, 1.0 }, { x, 1.0, 1.0 }
        };
        const vtkIdType base = points->GetNumberOfPoints();
        for (const auto& corner : corners)
            points->InsertNextPoint(corner);
        vtkIdType point_ids[8];
        for (vtkIdType i = 0; i < 8; ++i)
            point_ids[i] = base + i;
        cells->InsertNextCell(8, point_ids);
        types->InsertNextValue(VTK_HEXAHEDRON);
        original_ids->InsertNextValue(cell_id);
        weights->InsertNextValue(10.0 * static_cast<double>(cell_id));
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);
    grid->SetCells(types, cells);
    grid->GetCellData()->AddArray(original_ids);
    grid->GetCellData()->AddArray(weights);
    return grid;
}

//! @brief 用一次全新的过滤器执行隐藏，返回自持的输出（后续重建不会改写它）。
vtkSmartPointer<vtkUnstructuredGrid> runFilter(vtkUnstructuredGrid* input, const std::vector<vtkIdType>& hidden)
{
    vtkNew<SolidHideFilter> filter;
    vtkNew<vtkIdTypeArray> hidden_ids;
    for (auto id : hidden)
        hidden_ids->InsertNextValue(id);
    filter->SetInputData(input);
    filter->SetHiddenCellIds(hidden_ids);
    REQUIRE(filter->Update(0, nullptr));
    // 返回值持有一份引用，过滤器析构后输出仍然有效。
    return filter->GetOutput();
}

} // namespace

TEST_CASE("SolidHideFilter keeps original typed solid ids and attributes aligned")
{
    auto grid = makeHexGrid(4);
    const auto output = runFilter(grid, { 1, 3 });

    // 隐藏后剩余单元仍报告原始体 ID，且身份数组未被提升为浮点数组。
    REQUIRE(output->GetNumberOfCells() == 2);
    REQUIRE(solidIdValues(output) == std::vector<vtkIdType> { 0, 2 });

    // 单元属性必须与保留体一一对应，不能整体平移或错位。
    auto* weights = vtkDoubleArray::SafeDownCast(output->GetCellData()->GetArray("solidWeight"));
    REQUIRE(weights != nullptr);
    CHECK(weights->GetValue(0) == 0.0);
    CHECK(weights->GetValue(1) == 20.0);
}

TEST_CASE("SolidHideFilter ignores invalid ids and yields a valid empty result")
{
    auto grid = makeHexGrid(4);

    // 重复 ID 只移除一次，越界与负 ID 一律忽略。
    const auto partial = runFilter(grid, { 0, 0, -1, 4, 100 });
    REQUIRE(partial->GetNumberOfCells() == 3);
    REQUIRE(solidIdValues(partial) == std::vector<vtkIdType> { 1, 2, 3 });

    // 全部隐藏必须得到合法空结果，而不是退化成透传完整输入。
    const auto empty = runFilter(grid, { 0, 1, 2, 3 });
    REQUIRE(empty->GetNumberOfCells() == 0);
    REQUIRE(empty->GetPoints() != nullptr);
    if (auto* weights = empty->GetCellData()->GetArray("solidWeight"))
        CHECK(weights->GetNumberOfTuples() == 0);

    // 后续重建不会改写已取出的旧结果。
    const auto repeated = runFilter(grid, { 1, 3 });
    REQUIRE(repeated->GetNumberOfCells() == 2);
    REQUIRE(partial->GetNumberOfCells() == 3);
    REQUIRE(solidIdValues(partial) == std::vector<vtkIdType> { 1, 2, 3 });
}

TEST_CASE("SolidHideFilter preserves polyhedron face topology")
{
    vtkNew<vtkPoints> points;
    const double hex_corners[8][3] = {
        { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 }, { 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 1.0 }, { 1.0, 1.0, 1.0 }, { 0.0, 1.0, 1.0 }
    };
    const double poly_corners[8][3] = {
        { 2.0, 0.0, 0.0 }, { 3.0, 0.0, 0.0 }, { 3.0, 1.0, 0.0 }, { 2.0, 1.0, 0.0 },
        { 2.0, 0.0, 1.0 }, { 3.0, 0.0, 1.0 }, { 3.0, 1.0, 1.0 }, { 2.0, 1.0, 1.0 }
    };
    for (const auto& corner : hex_corners)
        points->InsertNextPoint(corner);
    for (const auto& corner : poly_corners)
        points->InsertNextPoint(corner);

    vtkNew<vtkCellArray> cells;
    vtkNew<vtkUnsignedCharArray> types;
    vtkNew<vtkIdTypeArray> original_ids;
    original_ids->SetName("vtkOriginalCellIds");

    vtkIdType hex_ids[8];
    for (vtkIdType i = 0; i < 8; ++i)
        hex_ids[i] = i;
    cells->InsertNextCell(8, hex_ids);
    types->InsertNextValue(VTK_HEXAHEDRON);
    original_ids->InsertNextValue(0);

    vtkIdType poly_ids[8];
    for (vtkIdType i = 0; i < 8; ++i)
        poly_ids[i] = 8 + i;
    cells->InsertNextCell(8, poly_ids);
    types->InsertNextValue(VTK_POLYHEDRON);
    original_ids->InsertNextValue(1);

    // 立方体 6 个四边形面：faces 存面拓扑，faceLocations 按单元给出一份面 id 列表。
    vtkNew<vtkCellArray> faces;
    const vtkIdType face_quads[6][4] = {
        { 8, 9, 10, 11 }, { 12, 13, 14, 15 }, { 8, 9, 13, 12 },
        { 9, 10, 14, 13 }, { 10, 11, 15, 14 }, { 11, 8, 12, 15 }
    };
    for (const auto& quad : face_quads)
        faces->InsertNextCell(4, quad);
    vtkNew<vtkCellArray> face_locations;
    // 六面体没有面列表，用空条目占位；多面体列出它用到的 6 个面 id。
    face_locations->InsertNextCell(0, nullptr);
    vtkIdType face_ids[6] = { 0, 1, 2, 3, 4, 5 };
    face_locations->InsertNextCell(6, face_ids);

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);
    grid->SetPolyhedralCells(types, cells, face_locations, faces);
    grid->GetCellData()->AddArray(original_ids);

    const auto output = runFilter(grid, { 0 });
    REQUIRE(output->GetNumberOfCells() == 1);
    REQUIRE(output->GetCellType(0) == VTK_POLYHEDRON);
    // 先确认输入夹具本身是合法多面体，否则断言没有意义。
    vtkNew<vtkGenericCell> input_cell;
    grid->GetCell(1, input_cell);
    REQUIRE(input_cell->GetNumberOfFaces() == 6);
    REQUIRE(input_cell->GetNumberOfPoints() == 8);
    // 面拓扑必须完整搬移，而不是被压成普通点列表。
    vtkNew<vtkGenericCell> cell;
    output->GetCell(0, cell);
    CHECK(cell->GetNumberOfFaces() == 6);
    CHECK(cell->GetNumberOfPoints() == 8);
    for (vtkIdType face = 0; face < cell->GetNumberOfFaces(); ++face)
        CHECK(cell->GetFace(face)->GetNumberOfPoints() == 4);
    REQUIRE(solidIdValues(output) == std::vector<vtkIdType> { 1 });
}

TEST_CASE("MeshPipelineData hides solids without compressing pick ids")
{
    MeshData mesh;
    MeshPipelineData data;
    data.loadModelData(MakeMeshDataVtk(mesh));

    const auto original = data.createSnapshot();
    const auto all_ids = uniqueSolidIds(original->solid_surface);
    REQUIRE(all_ids.size() >= 2);

    // 隐藏其中一个体：幸存体身份必须仍是原始体 ID，不能改成紧凑索引。
    const std::vector<vtkIdType> first_hidden { all_ids[0] };
    data.hideSolids(first_hidden);
    const auto hidden = data.createSnapshot();
    REQUIRE(uniqueSolidIds(hidden->solid_surface) == withoutIds(all_ids, first_hidden));

    // 再次隐藏必须累积：全部隐藏后得到空表面。
    data.hideSolids({ all_ids[1] });
    REQUIRE(data.createSnapshot()->solid_surface->GetNumberOfCells() == 0);

    // 恢复后回到完整集合，且隐藏体不参与内部线框。
    data.showAllSolids();
    const auto restored = data.createSnapshot();
    CHECK(uniqueSolidIds(restored->solid_surface) == all_ids);
    CHECK(restored->solid_internal_edges->GetNumberOfCells()
        >= hidden->solid_internal_edges->GetNumberOfCells());
}

TEST_CASE("MeshPipelineData isolates selected solids and restores them")
{
    MeshData mesh;
    MeshPipelineData data;
    data.loadModelData(MakeMeshDataVtk(mesh));

    const auto original = data.createSnapshot();
    const auto all_ids = uniqueSolidIds(original->solid_surface);

    const std::vector<vtkIdType> target { all_ids[1] };
    data.isolateSolids(target);
    const auto isolated = data.createSnapshot();
    REQUIRE(uniqueSolidIds(isolated->solid_surface) == target);
    CHECK(isolated->visible);

    data.showAllSolids();
    CHECK(uniqueSolidIds(data.createSnapshot()->solid_surface) == all_ids);

    // 体隔离不影响面隐藏状态。
    data.hideFaces({ 0 });
    CHECK(data.createSnapshot()->visible_faces->GetNumberOfCells()
        < original->visible_faces->GetNumberOfCells());
}

TEST_CASE("MeshPipelineData reverse complements face and solid hidden sets independently")
{
    MeshData mesh;
    MeshPipelineData data;
    data.loadModelData(MakeMeshDataVtk(mesh));

    const auto original = data.createSnapshot();
    const auto all_ids = uniqueSolidIds(original->solid_surface);
    REQUIRE(all_ids.size() >= 2);

    // 只隐藏体、面隐藏集为空：体取补，面保持不变（空集合不参与取补）。
    data.hideSolids({ all_ids[0] });
    REQUIRE(data.reverseVisibility());

    const auto reversed = data.createSnapshot();
    CHECK(uniqueSolidIds(reversed->solid_surface) == std::vector<vtkIdType> { all_ids[0] });
    CHECK(reversed->visible_faces->GetNumberOfCells() == original->visible_faces->GetNumberOfCells());

    // 再反转一次回到只隐藏一个体的状态（补集对合）。
    REQUIRE(data.reverseVisibility());
    const auto again = data.createSnapshot();
    CHECK(uniqueSolidIds(again->solid_surface) == withoutIds(all_ids, { all_ids[0] }));

    // 整体关闭时反转：恢复显示并清空局部隐藏集合。
    data.setVisibility(false);
    REQUIRE(data.reverseVisibility());
    const auto shown = data.createSnapshot();
    CHECK(shown->visible);
    CHECK(uniqueSolidIds(shown->solid_surface) == all_ids);
}

TEST_CASE("MeshPipelineData clears solid hiding on reload and keeps old snapshots")
{
    MeshData mesh;
    MeshPipelineData data;
    data.loadModelData(MakeMeshDataVtk(mesh));

    const auto original = data.createSnapshot();
    const auto all_ids = uniqueSolidIds(original->solid_surface);
    const auto generation = data.generation();

    const std::vector<vtkIdType> hidden_ids { all_ids[0] };
    data.hideSolids(hidden_ids);
    const auto hidden = data.createSnapshot();
    REQUIRE(hidden->generation == generation);
    const auto hidden_visible = uniqueSolidIds(hidden->solid_surface);
    REQUIRE(hidden_visible == withoutIds(all_ids, hidden_ids));

    data.loadModelData(MakeMeshDataVtk(mesh));
    const auto reloaded = data.createSnapshot();
    CHECK(reloaded->generation != generation);
    CHECK(uniqueSolidIds(reloaded->solid_surface) == all_ids);

    // 旧快照仍独立保活已发布的隐藏结果，不被重载改写。
    CHECK(uniqueSolidIds(hidden->solid_surface) == hidden_visible);
}

TEST_CASE("MeshPipelineData reuses visible solids cache across clip and face changes")
{
    MeshData mesh;
    MeshPipelineData data;
    data.loadModelData(MakeMeshDataVtk(mesh));

    const auto original = data.createSnapshot();
    auto* edges = original->solid_internal_edges.GetPointer();
    REQUIRE(edges->GetNumberOfCells() > 0);
    const vtkIdType original_edge_count = edges->GetNumberOfCells();
    const vtkIdType original_surface_count = original->solid_surface->GetNumberOfCells();

    // 只移动裁剪平面：不重新过滤体、不重新提边。
    MeshClipPlane plane;
    plane.enabled = true;
    plane.origin = { -1.0e6, 0.0, 0.0 };
    data.setClipPlane(plane);
    const auto clipped = data.createSnapshot();
    REQUIRE(clipped->solid_surface->GetNumberOfCells() == 0);
    CHECK(clipped->solid_internal_edges == edges);

    data.setClipPlane(nullptr);
    CHECK(data.createSnapshot()->solid_internal_edges == edges);

    // 只隐藏面：内部线框与体表面都不受影响。
    data.hideFaces({ 0 });
    CHECK(data.createSnapshot()->solid_internal_edges == edges);

    // 只切换整体显隐：可见体网格不变。
    data.showAllFaces();
    data.setVisibility(false);
    CHECK(data.createSnapshot()->solid_internal_edges == edges);
    data.setVisibility(true);
    CHECK(data.createSnapshot()->solid_internal_edges == edges);

    // 隐藏体集合改变：必须重新计算，且新结果反映隐藏后的体。
    const auto all_ids = uniqueSolidIds(original->solid_surface);
    data.hideSolids({ all_ids[0] });
    const auto hidden = data.createSnapshot();
    CHECK(hidden->solid_internal_edges != edges);
    CHECK(hidden->solid_internal_edges->GetNumberOfCells() < original_edge_count);
    CHECK(hidden->solid_surface->GetNumberOfCells() < original_surface_count);

    // 恢复隐藏体后重新计算，回到完整线框。
    data.showAllSolids();
    const auto restored = data.createSnapshot();
    CHECK(restored->solid_internal_edges != hidden->solid_internal_edges);
    CHECK(restored->solid_internal_edges->GetNumberOfCells() == original_edge_count);

    // 已发布快照不被后续重建改写。
    CHECK(edges->GetNumberOfCells() == original_edge_count);
    CHECK(original->solid_surface->GetNumberOfCells() == original_surface_count);
}
