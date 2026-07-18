/**
* @file：QRenderWindowStyle.h
* @brief：渲染窗口中的交互操作
* @author：付轩宇 982531420@qq.com

*/
#ifndef Q_RENDER_WINDOW_STYLE_H
#define Q_RENDER_WINDOW_STYLE_H
#include <vtkInteractorStyleTrackballCamera.h>

class SelectManager;
class MeasureManager;

class QRenderWindowStyle : public vtkInteractorStyleTrackballCamera {
public:
	static QRenderWindowStyle* New();
    vtkTypeMacro(QRenderWindowStyle, vtkInteractorStyleTrackballCamera);
    void SetClick();
    void SetSelectManager(SelectManager* select_manager);
    void SetMeasureManager(MeasureManager* measure_manager);
    //! @brief 测量模式下点击走测量拾取、悬停做动态预览，不再触发选择
    void SetMeasureMode(bool on);
    void OnLeftButtonUp() override;
    void OnMouseMove() override;

private:
    bool click_ {};
    bool measure_mode_ {};
    SelectManager* select_manager_{};
    MeasureManager* measure_manager_{};
};
#endif // Q_RENDER_WINDOW_STYLE_H
