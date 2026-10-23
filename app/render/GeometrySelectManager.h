#ifndef GEOMETRY_SELECT_MANAGER_H
#define GEOMETRY_SELECT_MANAGER_H
#include "Core.h"
#include "Selection.h"
#include "GeometrySelectorHighlight.h"
#include "GeometryActorSelectOp.h"

#include <memory>
#include <unordered_map>
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkActor.h>

class vtkRenderer;
class vtkPartitionedDataSet;
class vtkCompositePolyDataMapper;
class GeometryActorManagerSelectOp;

class GeometrySelectManager {
public:
    GeometrySelectManager(vtkRenderer& renderer, vtkActor& highlight_actor, GeometryActorManagerSelectOp& op);

    void select(double posx, double posy, SelectOp op = SelectOp::Toggle);

    /**
     * @brief 几何框选：矩形区域内的几何元素（面/边/顶点/体）
     *        框选是否穿透由 penetrating_ 成员决定（UI 开关经 setPenetrating 下发）。
     * @note 框选永不清空——由各 selector 按 op 做集合运算（Append->并集 / Remove->差集 / Toggle->对称差）。
     * @note OCC 的 IVtkTools_ShapePicker 在框选时忽略 theIsAll 参数（其文档明确说明），
     *       故非穿透不能靠切换该参数实现，必须另走 VTK 硬件拾取。
     */
    void selectArea(int xmin, int ymin, int xmax, int ymax, SelectOp op = SelectOp::Toggle);
    void setSelectMode(SelectMode select_mode);

    //! @brief 设置框选是否穿透
    void setPenetrating(bool enabled);
    bool isPenetrating() const;
    void clearSelection();
    std::unique_ptr<Selection> getSelection();
    void setHighlightVisible(bool visible);
    void setHighlightVisible(Index component_id, bool visible);

    /**
     * @brief 吸附几何顶点：拾取并解析为几何顶点
     * @return 命中时返回 {GeometryRegistry 顶点 id, 世界坐标}，未命中返回 std::nullopt
     */
    std::optional<std::pair<Index, std::array<double, 3>>> snapGeometryVertex(double posx, double posy);

private:
    GeometrySelectorHighlight* getOrCreateSelector(Index component_id);

    /**
     * @brief 非穿透框选：用 VTK 硬件拾取只取可见 cell，再把 cell 换算成几何子形状。
     */
    void selectAreaNonPenetrating(int xmin, int ymin, int xmax, int ymax, SelectOp op);
    void applyHighlightStyle(SelectMode mode);

    GeometryActorManagerSelectOp* op_;
    SelectMode select_mode_ { SelectMode::None };
    vtkRenderer* renderer_;
    vtkSmartPointer<IVtkTools_ShapePicker> picker_;

    vtkActor* highlight_actor_ {};
    vtkSmartPointer<vtkPartitionedDataSet> highlight_data_;
    vtkSmartPointer<vtkCompositePolyDataMapper> highlight_mapper_;
    bool highlight_visible_ { true };
    bool penetrating_ { false }; 

    std::unordered_map<Index, std::unique_ptr<GeometrySelectorHighlight>> component_selectors_;
};

#endif
