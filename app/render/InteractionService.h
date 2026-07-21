/**
 * @file InteractionService.h
 * @brief 通用视口交互服务：拾取解析 + 标注绘制 + 事件路由，与具体插件解耦
 */
#ifndef INTERACTION_SERVICE_H
#define INTERACTION_SERVICE_H

#include "InteractiveTypes.h"

#include <functional>
#include <string>
#include <vector>
#include <vtkSmartPointer.h>

class vtkRenderer;
class vtkActor;
class vtkPolyData;
class vtkHardwarePicker;
class vtkBillboardTextActor3D;
class MeshActorManagerSelectOp;
class GeometryActorManagerSelectOp;
class IVtkTools_ShapePicker;

namespace systems::interaction {
class InteractiveHandler;
}

/**
 * @brief 通用视口交互服务
 *
 * start 某个 InteractiveHandler 后，将左键/悬停解析为 PickInfo 转发给它，
 * 并按其回传的 AnnotationBatch 统一绘制标注（点/实线/虚线/叠加层文本）。
 * 与选择系统的互斥由 QRenderWindowStyle 按 isActive() 路由。
 */
class InteractionService {
public:
    InteractionService(vtkRenderer& renderer, vtkRenderer& overlay_renderer,
        MeshActorManagerSelectOp& mesh_op, GeometryActorManagerSelectOp& geom_op,
        IVtkTools_ShapePicker& geom_picker);
    ~InteractionService();

    //! @brief 启动某个交互处理器（几何拾取切到顶点模式，handler onActivate 后刷新标注）
    void start(systems::interaction::InteractiveHandler* handler);
    //! @brief 停止交互（handler onDeactivate、还原几何拾取模式、清空标注）
    void stop();
    bool isActive() const { return handler_ != nullptr; }

    //! @brief 左键点击：解析吸附点并转发 handler
    void pick(double posx, double posy);
    //! @brief 悬停：转发 handler 做动态预览
    void hover(double posx, double posy);
    //! @brief 清空 handler 状态与全部标注
    void clear();

    //! @brief 结果文本变化回调（由 QRenderWindow 设置为发 Qt 信号），渲染线程内触发
    std::function<void(const std::string&)> onResultChanged;

private:
    //! @brief 网格顶点优先、几何顶点兜底的吸附解析；命中时 out.valid 置 true
    bool snapToPickInfo(double posx, double posy, systems::interaction::PickInfo& out);
    //! @brief 拉取 handler 标注集并刷新全部 actor
    void refreshAnnotations();
    void emitResult();

    vtkRenderer* renderer_ {};
    vtkRenderer* overlay_renderer_ {};
    MeshActorManagerSelectOp* mesh_op_ {};
    GeometryActorManagerSelectOp* geom_op_ {};
    IVtkTools_ShapePicker* geom_picker_ {};

    systems::interaction::InteractiveHandler* handler_ {};

    vtkSmartPointer<vtkHardwarePicker> mesh_picker_;

    vtkSmartPointer<vtkPolyData> points_poly_;
    vtkSmartPointer<vtkActor> points_actor_;
    vtkSmartPointer<vtkPolyData> lines_poly_;
    vtkSmartPointer<vtkActor> lines_actor_;
    vtkSmartPointer<vtkPolyData> dashed_poly_;
    vtkSmartPointer<vtkActor> dashed_actor_;

    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> text_pool_;
};

#endif // INTERACTION_SERVICE_H
