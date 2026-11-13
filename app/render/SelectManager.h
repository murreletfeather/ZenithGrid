#ifndef SELECT_MANAGER_H
#define SELECT_MANAGER_H
#include "Core.h"
#include "MeshSelectManager.h"
#include "Selection.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vtkNew.h>
#include <vtkActor.h>
#include <vtkType.h>

class vtkRenderer;
class GeometrySelectManager;
class MeshActorManagerSelectOp;
class GeometryActorManagerSelectOp;
class ComponentSelectorHighlight;
class IMeshIdQuery;

class SelectManager {
public:
    SelectManager(vtkRenderer& renderer,
        MeshActorManagerSelectOp& mesh_op, GeometryActorManagerSelectOp& geom_op);
    ~SelectManager();
    void select(double posx, double posy, SelectOp op = SelectOp::Toggle);
    /**
     * @brief 矩形框选入口（Ctrl+左键拖拽）：按当前选择模式转发给网格、几何或组件选择管理器。
     *        网格、几何、组件三种模式均按 op（Append/Remove/Toggle）做集合运算。
     *        网格模式的穿透与否由 setPenetrating() 控制（UI 开关）。
     */
    void selectArea(int xmin, int ymin, int xmax, int ymax, SelectOp op = SelectOp::Toggle);

    /**
     * @brief 设置网格框选是否穿透（穿透与非穿透并存，由 UI 开关决定走哪条）
     * @param enabled true  穿透：视锥几何求交，框内前层与背面单元一并命中，并额外对体网格
     *                      求交，可选中内部体单元/内部节点/内部边；
     *                false 非穿透：硬件拾取，受深度遮挡只命中可见最前一层
     * @note 仅对网格模式生效；单选恒为非穿透。
     */
    void setPenetrating(bool enabled);
    //! @brief 网格框选当前是否穿透
    bool isPenetrating() const;

    void setSelectMode(const std::string& select_mode);
    /**
     * @brief 设置网格面选择的角度扩散参数
     * @param enabled 是否启用按角度扩散
     * @param angle_deg 相邻面法向夹角阈值，单位为度
     */
    void setFaceSelectionByAngle(bool enabled, double angle_deg);
    //! @brief 吸附几何顶点：命中返回 {GeometryRegistry 顶点 id, 世界坐标}（转发给几何选择管理器）
    std::optional<std::pair<Index, std::array<double, 3>>> snapGeometryVertex(double posx, double posy);
    //! @brief 吸附网格顶点：命中返回组件 id、局部点 id 与世界坐标（转发给网格选择管理器）
    std::optional<MeshVertexSnap> snapMeshVertex(double posx, double posy);
    void clearSelection();
    /**
     * @brief 导出各组件保留的原始模型面 ID，包含已隐藏的选择（仅面选择模式有效）
     * @return component_id → 原模型面 id 列表；非面选择模式返回空表
     */
    std::unordered_map<Index, std::vector<vtkIdType>> selectedFaceIdsByComponent();
    //! @brief 注入模型层 id 查询接口，供选择器解析单元 id（如边端点对 -> 边 id）
    void setMeshIdQuery(const IMeshIdQuery* id_query);
    void refreshComponentHighlight();
    void setGeometryHighlightVisible(bool visible);
    void setGeometryHighlightVisible(Index component_id, bool visible);
    void setMeshHighlightVisible(bool visible);
    void setMeshHighlightVisible(Index component_id, bool visible);
    std::unique_ptr<Selection> getSelection();

private:
    std::unique_ptr<MeshSelectManager> mesh_;
    std::unique_ptr<GeometrySelectManager> geom_;
    std::unique_ptr<ComponentSelectorHighlight> component_selector_;
    SelectMode select_mode_ { SelectMode::None };
    bool penetrating_ { false };
    vtkRenderer* renderer_ {};
    vtkNew<vtkActor> highlight_actor_;
};

#endif
