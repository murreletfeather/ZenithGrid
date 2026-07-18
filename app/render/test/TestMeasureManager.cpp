/**
 * @file TestMeasureManager.cpp
 * @brief 交互式测量的可视化测试：两点成线、共点夹角；含程序化拾取自检
 */
#include "MakeMeshDataVtk.h"
#include "MeasureManager.h"
#include "GeometryActorManager.h"
#include "GeometryActorSelectOp.h"
#include "GeometryRegistry.h"
#include "GeometrySelectManager.h"
#include "GeometrySubshapeIndex.h"
#include "MeshActorManager.h"
#include "Selection.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <IVtkTools_ShapePicker.hxx>
#include <NCollection_List.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>

#include <spdlog/spdlog.h>
#include <vtkInteractorObserver.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

#include <vector>

// 自定义交互器：左键确认测量点，悬停动态预览
class MeasurePickInteractorStyle : public vtkInteractorStyleTrackballCamera {
public:
    static MeasurePickInteractorStyle* New();
    vtkTypeMacro(MeasurePickInteractorStyle, vtkInteractorStyleTrackballCamera);

    void SetMeasureManager(MeasureManager* measure_manager)
    {
        this->measure_manager_ = measure_manager;
    }

    void OnLeftButtonUp() override
    {
        int* pos = this->GetInteractor()->GetEventPosition();
        if (measure_manager_) {
            measure_manager_->pick(pos[0], pos[1]);
            this->GetInteractor()->Render();
        }
        vtkInteractorStyleTrackballCamera::OnLeftButtonUp();
    }

    void OnMouseMove() override
    {
        vtkInteractorStyleTrackballCamera::OnMouseMove();
        if (measure_manager_ && this->State == VTKIS_NONE) {
            int* pos = this->GetInteractor()->GetEventPosition();
            measure_manager_->hover(pos[0], pos[1]);
            this->GetInteractor()->Render();
        }
    }

private:
    MeasureManager* measure_manager_ {};
};

vtkStandardNewMacro(MeasurePickInteractorStyle);

int main(int argc, char* argv[])
{
    MeshData mesh;
    MeshDataVtk test_mesh_data = MakeMeshDataVtk(mesh);

    vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();
    renderer->SetBackground(0.2, 0.3, 0.4);

    vtkSmartPointer<vtkRenderWindow> renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(600, 600);

    // 叠加渲染层：测量文字标注置顶，不被模型遮挡（SetLayer(1) 自动 PreserveColorBuffer）
    renderWindow->SetNumberOfLayers(2);
    vtkSmartPointer<vtkRenderer> overlay_renderer = vtkSmartPointer<vtkRenderer>::New();
    overlay_renderer->SetLayer(1);
    overlay_renderer->InteractiveOff();
    overlay_renderer->SetActiveCamera(renderer->GetActiveCamera());
    renderWindow->AddRenderer(overlay_renderer);

    vtkSmartPointer<vtkRenderWindowInteractor> interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    interactor->SetRenderWindow(renderWindow);

    vtkSmartPointer<MeasurePickInteractorStyle> style = vtkSmartPointer<MeasurePickInteractorStyle>::New();
    interactor->SetInteractorStyle(style);

    vtkNew<vtkPoints> pts;
    pts->SetNumberOfPoints(static_cast<vtkIdType>(mesh.vertex_positions_.size()));
    for (size_t i = 0; i < mesh.vertex_positions_.size(); ++i) {
        pts->SetPoint(static_cast<vtkIdType>(i), mesh.vertex_positions_[i].data());
    }

    MeshActorManager mesh_manager(pts.GetPointer());
    mesh_manager.bindRender(renderer.GetPointer());
    GeometryActorManager geometry_manager;
    geometry_manager.bindRender(renderer.GetPointer());

    // 与应用一致：几何拾取器全程序单一实例，测量复用选择系统的 picker
    vtkNew<vtkActor> sel_highlight_actor;
    sel_highlight_actor->PickableOff();
    renderer->AddActor(sel_highlight_actor);
    GeometrySelectManager sel_mgr(*renderer, *sel_highlight_actor, geometry_manager.op());

    // 先构造测量管理器（注册 pick list）再加载网格，保证 actor 进入拾取列表
    MeasureManager measure_manager(*renderer, *overlay_renderer, mesh_manager.op(),
        geometry_manager.op(), *sel_mgr.picker());
    measure_manager.setActive(true);
    measure_manager.onChanged = [&measure_manager]() {
        spdlog::info("lines: {}, result:\n{}", measure_manager.lineCount(), measure_manager.resultText());
    };
    style->SetMeasureManager(&measure_manager);

    mesh_manager.loadMesh(0, test_mesh_data, renderer.GetPointer(), ModelRenderMode::Face);

    // 自检：投影 3 个顶点到屏幕坐标，程序化拾取构成两条共点线，应得 2 条线与 1 个夹角
    renderer->ResetCamera();
    renderWindow->Render();
    if (mesh.vertex_positions_.size() >= 3) {
        // 逐次拾取并渲染（模拟真实交互的帧间渲染），记录每步状态
        auto pick_vertex = [&](size_t index) {
            const auto& wp = mesh.vertex_positions_[index];
            double display[3];
            vtkInteractorObserver::ComputeWorldToDisplay(renderer.GetPointer(), wp[0], wp[1], wp[2], display);
            measure_manager.pick(display[0], display[1]);
            spdlog::info("pick v{} -> lines={}, pending={}", index,
                measure_manager.lineCount(), measure_manager.hasPending());
            renderWindow->Render();
        };
        pick_vertex(0);
        pick_vertex(1);
        pick_vertex(1);
        pick_vertex(2);
        const bool ok = measure_manager.lineCount() == 2
            && measure_manager.resultText().find("夹角") != std::string::npos;
        spdlog::info("measure self-check {}: lines={}, result:\n{}",
            ok ? "PASS" : "FAIL", measure_manager.lineCount(), measure_manager.resultText());
    }

    // ============ 几何（OCC，iges/stp 场景）拾取自检 ============
    // 大 Box 放远处避开演示网格：顶点投影间距需大于拾取容差(40px)，
    // 否则两次拾取会被吸附到同一顶点而无法成线
    {
        TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(500.0, 500.0, 0.0), 1000.0, 800.0, 600.0).Shape();
        GeometryRegistry geom_reg;
        GeometrySubshapeIndex geom_index;
        geom_index.build(box, geom_reg);
        const Index geom_component_id = 999;
        GeometryDataVtk geom_data { box, geom_component_id, &geom_index };
        geometry_manager.loadGeometry(geom_data);

        // 收集 Box 全部顶点
        std::vector<std::array<double, 3>> box_vertices;
        for (TopExp_Explorer exp(box, TopAbs_VERTEX); exp.More(); exp.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
            box_vertices.push_back({ p.X(), p.Y(), p.Z() });
        }
        spdlog::info("geometry self-check: box vertices={}", box_vertices.size());

        renderer->ResetCamera();
        renderWindow->Render();

        auto to_display = [&](const std::array<double, 3>& wp, double out[2]) {
            double d[3];
            vtkInteractorObserver::ComputeWorldToDisplay(renderer.GetPointer(), wp[0], wp[1], wp[2], d);
            out[0] = d[0];
            out[1] = d[1];
        };

        // 端到端：pick 两个几何顶点，应新增一条线
        if (box_vertices.size() >= 2) {
            const int before = measure_manager.lineCount();
            double d[2];
            to_display(box_vertices[0], d);
            measure_manager.pick(d[0], d[1]);
            spdlog::info("geom pick v0 -> lines={}, pending={}",
                measure_manager.lineCount(), measure_manager.hasPending());
            renderWindow->Render();
            to_display(box_vertices[1], d);
            measure_manager.pick(d[0], d[1]);
            spdlog::info("geom pick v1 -> lines={}, pending={}",
                measure_manager.lineCount(), measure_manager.hasPending());
            renderWindow->Render();
            spdlog::info("geometry end-to-end {}: lines={}",
                measure_manager.lineCount() == before + 1 ? "PASS" : "FAIL",
                measure_manager.lineCount());
        }

        // geom_index 等局部对象须存活到交互结束
        renderWindow->Render();
        interactor->Start();
    }

    return 0;
}
