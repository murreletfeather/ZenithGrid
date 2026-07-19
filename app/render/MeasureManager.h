/**
 * @file MeasureManager.h
 * @brief 交互式测量管理器，仿 HyperMesh：两点成线显示长度，共端点的两线自动显示夹角
 * @author 范成通 email 1941804585@qq.com
 */
#ifndef MEASURE_MANAGER_H
#define MEASURE_MANAGER_H

#include "Core.h"

#include <array>
#include <functional>
#include <optional>
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

/**
 * @brief 交互式测量管理器
 *
 * 激活后在视图中点击即可吸附拾取网格顶点或几何顶点：两点构成一条直线并标注长度，
 * 可连续画多条；新线与已有线共端点时自动在共点处标注夹角。
 * 文字标注放在叠加渲染层，始终置顶不被模型遮挡。
 * 仅负责渲染线程内的拾取、标注与计算，通过 onChanged 回调向 Qt/QML 侧通报结果。
 */
class MeasureManager {
public:
    //! @param geom_picker 与选择系统共用的几何拾取器：同一形状在多个 IVtkTools_ShapePicker
    //! 实例上激活同一选择模式时，仅最先激活的实例能正常拾取，因此全程序保持单一实例
    MeasureManager(vtkRenderer& renderer, vtkRenderer& overlay_renderer,
        MeshActorManagerSelectOp& mesh_op, GeometryActorManagerSelectOp& geom_op,
        IVtkTools_ShapePicker& geom_picker);
    ~MeasureManager();

    //! @brief 进入/退出交互测量模式（进入时几何拾取切到顶点模式，退出时还原）
    void setActive(bool on);
    bool isActive() const { return active_; }

    //! @brief 左键点击：吸附拾取端点；首点后再点成线，点同一点两次取消起笔
    void pick(double posx, double posy);

    //! @brief 鼠标悬停：已有首点时动态预览到吸附点的虚线与距离
    void hover(double posx, double posy);

    //! @brief 清空全部测量线与标注
    void clear();

    int lineCount() const { return static_cast<int>(lines_.size()); }
    bool hasPending() const { return pending_.has_value(); }

    //! @brief 面板显示文本（全部线长与夹角）
    std::string resultText() const;

    //! @brief 测量状态变化回调（由 QRenderWindow 设置为发 Qt 信号），渲染线程内触发
    std::function<void()> onChanged;

private:
    //! @brief 测量端点：世界坐标 + 两套顶点 id（用于跨拾取判同一点）
    struct MeasurePoint {
        std::array<double, 3> pos;
        Index mesh_id = -1; //> 网格全局顶点 id（vtkOriginalPointIds），非网格点为 -1
        Index geom_id = -1; //> 几何全局顶点 id（GeometryRegistry id），非几何点为 -1
    };
    struct MeasureLine {
        MeasurePoint a, b;
    };
    //! @brief 共端点两线的夹角：at 为共点，p/q 为两线各自另一端点
    struct MeasureAngle {
        int line1 = 0; //> lines_ 下标
        int line2 = 0;
        std::array<double, 3> at;
        std::array<double, 3> p;
        std::array<double, 3> q;
        double angle = 0.0;
    };

    static bool samePoint(const MeasurePoint& a, const MeasurePoint& b);
    //! @brief 网格顶点优先、几何顶点兜底的吸附拾取
    bool pickPoint(double posx, double posy, MeasurePoint& out);
    //! @brief 完成一条线：查重、入库、对共端点的已有线记录夹角
    void addLine(const MeasurePoint& a, const MeasurePoint& b);
    void updateActors();
    void updateHoverActors(const std::array<double, 3>* snapped);
    //! @brief 文字 actor 池：按需创建（加到叠加层），按序号复用
    vtkBillboardTextActor3D* getPooledText(
        std::vector<vtkSmartPointer<vtkBillboardTextActor3D>>& pool, size_t index,
        double r, double g, double b);

    vtkRenderer* renderer_ {};
    vtkRenderer* overlay_renderer_ {};
    MeshActorManagerSelectOp* mesh_op_ {};
    GeometryActorManagerSelectOp* geom_op_ {};

    vtkSmartPointer<vtkHardwarePicker> mesh_picker_;
    IVtkTools_ShapePicker* geom_picker_ {}; //> 与选择系统共用的唯一几何拾取器实例

    std::vector<MeasureLine> lines_;
    std::vector<MeasureAngle> angles_;
    std::optional<MeasurePoint> pending_; //> 已起笔未成线的首点
    bool active_ {};

    vtkSmartPointer<vtkPolyData> points_poly_;
    vtkSmartPointer<vtkActor> points_actor_;
    vtkSmartPointer<vtkPolyData> lines_poly_;
    vtkSmartPointer<vtkActor> lines_actor_;
    vtkSmartPointer<vtkPolyData> hover_poly_;
    vtkSmartPointer<vtkActor> hover_actor_;

    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> len_texts_;
    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> angle_texts_;
    vtkSmartPointer<vtkBillboardTextActor3D> hover_text_;
};

#endif // MEASURE_MANAGER_H
