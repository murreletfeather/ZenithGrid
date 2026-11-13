/** @file TestGeometryEdgePointPicking.cpp
 * @brief 边上位置的离屏拾取回归，不创建可见窗口或输出截图。
 */
#include "GeometryActorManager.h"
#include "GeometryDataVtk.h"
#include "GeometryRegistry.h"
#include "GeometrySubshapeIndex.h"
#include "MeshActorManager.h"
#include "SelectManager.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopoDS_Face.hxx>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkCamera.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>

namespace {
/** @brief 仅装配正式选择入口；导入索引离开作用域，验证渲染快照不借用原索引。 */
struct EdgeScene {
    vtkNew<vtkRenderer> renderer;
    vtkNew<vtkRenderWindow> window;
    GeometryRegistry registry;
    GeometryActorManager geometry;
    MeshActorManager mesh;
    std::unique_ptr<SelectManager> selection;

    EdgeScene()
    {
        window->SetOffScreenRendering(1);
        window->SetMultiSamples(0);
        window->SetSize(600, 600);
        window->AddRenderer(renderer);
        geometry.bindRender(renderer);
        mesh.bindRender(renderer);
        selection = std::make_unique<SelectManager>(*renderer, mesh.op(), geometry.op());
        auto* camera = renderer->GetActiveCamera();
        camera->SetPosition(5, 0, 20);
        camera->SetFocalPoint(5, 0, 0);
        camera->SetViewUp(0, 1, 0);
        camera->ParallelProjectionOn();
        camera->SetParallelScale(8);
        camera->SetClippingRange(0.01, 100);
    }
    /** @brief 加载后明确作废旧候选，返回本次首条边的全局身份。 */
    Index add(const TopoDS_Shape& shape, Index component = 1)
    {
        GeometrySubshapeIndex index;
        index.build(shape, registry);
        geometry.loadGeometry(GeometryDataVtk { shape, component, &index });
        selection->invalidateGeometry(component);
        window->Render();
        return index.edgeGlobalId(1);
    }
    /** @brief 已知世界位置映射为鼠标位置，允许施加屏幕像素偏移。 */
    std::array<double, 2> screen(double x, double y = 0)
    {
        renderer->SetWorldPoint(x, y, 0, 1);
        renderer->WorldToDisplay();
        const auto* p = renderer->GetDisplayPoint();
        return { p[0], p[1] };
    }
    /** @brief 通过不可拾取的点 Actor 核查显示数量，确保没有连接线。 */
    vtkIdType markers()
    {
        vtkIdType count = 0;
        auto* actors = renderer->GetActors();
        actors->InitTraversal();
        while (auto* actor = actors->GetNextActor()) {
            auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
            if (!actor->GetPickable() && actor->GetVisibility() && mapper) {
                auto* data = mapper->GetInput();
                REQUIRE(data);
                CHECK(data->GetNumberOfLines() == 0);
                count += data->GetNumberOfPoints();
            }
        }
        return count;
    }
};
}

TEST_CASE("Edge positions snap replace and clear through the ordinary selection entry")
{
    EdgeScene scene;
    const auto edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    const auto id = scene.add(edge);
    scene.selection->setSelectMode("GeometryPoint");
    auto p = scene.screen(3);
    scene.selection->hover(p[0], p[1] + 4);
    CHECK(scene.selection->getSelection()->size() == 0);
    CHECK(scene.markers() == 1);
    scene.selection->select(p[0], p[1] + 4);
    auto result = scene.selection->getSelection();
    REQUIRE(result->size() == 1);
    auto point = result->get<ElementEnum::GeometryPoint>()->front();
    CHECK(point.support_id == id);
    CHECK(point.component_id == 1);
    CHECK(point.edge_parameter == Catch::Approx(3).margin(1e-6));
    CHECK(point.world_pos[1] == Catch::Approx(0));
    CHECK(scene.markers() == 1);
    // 靠近端点仍取曲线内参数，不因端点吸附把合法分割位置变成顶点。
    p = scene.screen(0.05);
    scene.selection->select(p[0], p[1]);
    result = scene.selection->getSelection();
    REQUIRE(result->size() == 1);
    CHECK(result->get<ElementEnum::GeometryPoint>()->front().edge_parameter == Catch::Approx(0.05).margin(1e-6));
    scene.selection->clearMeshSelection();
    scene.selection->invalidateGeometry(2);
    CHECK(scene.selection->getSelection()->size() == 1);
    scene.selection->clearSelection();
    CHECK(scene.selection->getSelection()->size() == 0);
    CHECK(scene.markers() == 0);
    scene.selection->select(p[0], p[1]);
    CHECK(scene.selection->getSelection()->size() == 1);
    scene.selection->setSelectMode("GeometryVertex");
    p = scene.screen(0);
    REQUIRE(scene.selection->snapGeometryVertex(p[0], p[1]));
    scene.selection->select(p[0], p[1]);
    CHECK(scene.selection->getSelection()->type() == ElementEnum::GeometryVertex);
    CHECK(scene.selection->getSelection()->size() == 1);
}

TEST_CASE("Curved edge points use exact parameters and reject opaque foreground faces")
{
    EdgeScene scene;
    const auto edge = BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(5, 0, 0), gp_Dir(0, 0, 1)), 3)).Edge();
    scene.add(edge);
    scene.add(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(5, 0, 1), gp_Dir(0, 0, 1)), -4, 4, -4, 4).Face(), 2);
    scene.selection->setSelectMode("GeometryPoint");
    const double parameter = 0.37;
    const auto expected = BRepAdaptor_Curve(edge).Value(parameter);
    const auto p = scene.screen(expected.X(), expected.Y());
    scene.selection->select(p[0], p[1]);
    CHECK(scene.selection->getSelection()->size() == 0);
    scene.geometry.setVisibility(2, false);
    scene.selection->select(p[0], p[1]);
    // 清理临时显示后再次拾取，各档位仍使用相同的精确曲线与全局身份。
    for (int precision : { 0, 3, 1, 2 }) {
        scene.selection->clearSelection();
        scene.geometry.setSurfacePrecision(precision);
        scene.selection->select(p[0], p[1]);
        const auto result = scene.selection->getSelection();
        REQUIRE(result->size() == 1);
        const auto& point = result->get<ElementEnum::GeometryPoint>()->front();
        CHECK(point.edge_parameter == Catch::Approx(parameter).margin(1e-6));
        CHECK(gp_Pnt(point.world_pos[0], point.world_pos[1], point.world_pos[2]).Distance(expected) < 1e-6);
    }
}
