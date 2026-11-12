#ifndef GEOMETRY_SELECTOR_HIGHLIGHT_H
#define GEOMETRY_SELECTOR_HIGHLIGHT_H
#include "GeometryActorSelectOp.h"
#include "Selection.h"
#include "Core.h"

#include <unordered_map>
#include <unordered_set>
#include <vtkSmartPointer.h>

class vtkActor;
class vtkPolyData;
class vtkRenderer;
class vtkPartitionedDataSet;
class vtkMapper;
class IVtkTools_SubPolyDataFilter;

using GeometrySelectionVtk = Selection;

class GeometrySelectorHighlight {
public:
    virtual ~GeometrySelectorHighlight() = default;
    virtual void clear() = 0;
    virtual void disableHighlight() = 0;
    virtual void enableHighlight() = 0;
    virtual GeometrySelectionVtk get() const = 0;
};

/** @brief 精确点选择器持有当前确认点与悬停显示，不绘制业务路径。 */
class GeometryPointSelectorHighlight final : public GeometrySelectorHighlight {
public:
    /** @brief 为点选择创建不可拾取的显示对象，不依赖功能交互是否激活。 */
    explicit GeometryPointSelectorHighlight(vtkRenderer& renderer);
    ~GeometryPointSelectorHighlight() override;
    /** @brief 确认有效点并替换前一个分割位置。 */
    void select(const GeometryPoint& point);
    /** @brief 更新瞬时悬停点，空命中只移除悬停显示。 */
    void hover(const GeometryPoint& point);
    /** @brief 单个组件显隐只影响显示，不改变已确认的参数值。 */
    void setComponentVisible(Index component_id, bool visible);
    void clear() override;
    void disableHighlight() override;
    void enableHighlight() override;
    Selection get() const override;

private:
    /** @brief 从唯一确认值与瞬时悬停生成点显示。 */
    void updateHighlight();
    vtkRenderer* renderer_;
    vtkSmartPointer<vtkActor> actor_;
    vtkSmartPointer<vtkPolyData> data_;
    Selection selection_ { Selection::create<ElementEnum::GeometryPoint>({ }) };
    GeometryPoint hover_;
    bool visible_ { true };
    std::unordered_set<Index> hidden_components_;
};

class GeometryFaceSelectorHighlight final : public GeometrySelectorHighlight {
public:
    static void setupHighlightStyle(vtkActor& actor, vtkMapper& mapper);

    GeometryFaceSelectorHighlight(vtkPartitionedDataSet& highlight_data,
        unsigned int partition_id, GeometryActorSelectOp select_op);
    ~GeometryFaceSelectorHighlight() override;
    void toggle(IVtk_IdType subId, Index geomId, SelectOp op = SelectOp::Toggle);
    void clear() override;
    void disableHighlight() override;
    void enableHighlight() override;
    GeometrySelectionVtk get() const override;

private:
    vtkPartitionedDataSet* highlight_data_;
    unsigned int partition_id_;
    GeometryActorSelectOp select_op_;
    vtkSmartPointer<IVtkTools_SubPolyDataFilter> hl_filter_;
    std::unordered_map<IVtk_IdType, Index> selections_;
};

class GeometryEdgeSelectorHighlight final : public GeometrySelectorHighlight {
public:
    static void setupHighlightStyle(vtkActor& actor, vtkMapper& mapper);

    GeometryEdgeSelectorHighlight(vtkPartitionedDataSet& highlight_data,
        unsigned int partition_id, GeometryActorSelectOp select_op);
    ~GeometryEdgeSelectorHighlight() override;
    void toggle(IVtk_IdType subId, Index geomId, SelectOp op = SelectOp::Toggle);
    void clear() override;
    void disableHighlight() override;
    void enableHighlight() override;
    GeometrySelectionVtk get() const override;

private:
    vtkPartitionedDataSet* highlight_data_;
    unsigned int partition_id_;
    GeometryActorSelectOp select_op_;
    vtkSmartPointer<IVtkTools_SubPolyDataFilter> hl_filter_;
    std::unordered_map<IVtk_IdType, Index> selections_;
};

class GeometryVertexSelectorHighlight final : public GeometrySelectorHighlight {
public:
    static void setupHighlightStyle(vtkActor& actor, vtkMapper& mapper);

    GeometryVertexSelectorHighlight(vtkPartitionedDataSet& highlight_data,
        unsigned int partition_id, GeometryActorSelectOp select_op);
    ~GeometryVertexSelectorHighlight() override;
    void toggle(IVtk_IdType subId, Index geomId, SelectOp op = SelectOp::Toggle);
    void clear() override;
    void disableHighlight() override;
    void enableHighlight() override;
    GeometrySelectionVtk get() const override;

private:
    vtkPartitionedDataSet* highlight_data_;
    unsigned int partition_id_;
    GeometryActorSelectOp select_op_;
    vtkSmartPointer<IVtkTools_SubPolyDataFilter> hl_filter_;
    std::unordered_map<IVtk_IdType, Index> selections_;
};

class GeometrySolidSelectorHighlight final : public GeometrySelectorHighlight {
public:
    static void setupHighlightStyle(vtkActor& actor, vtkMapper& mapper);

    GeometrySolidSelectorHighlight(vtkPartitionedDataSet& highlight_data,
        unsigned int partition_id, GeometryActorSelectOp select_op);
    ~GeometrySolidSelectorHighlight() override;
    void toggleSolid(GeomSolidId solidId, const std::vector<IVtk_IdType>& faceSubIds, SelectOp op = SelectOp::Toggle);
    void clear() override;
    void disableHighlight() override;
    void enableHighlight() override;
    GeometrySelectionVtk get() const override;

private:
    vtkPartitionedDataSet* highlight_data_;
    unsigned int partition_id_;
    GeometryActorSelectOp select_op_;
    vtkSmartPointer<IVtkTools_SubPolyDataFilter> hl_filter_;
    std::unordered_map<IVtk_IdType, Index> selections_;
    std::unordered_set<IVtk_IdType> highlighted_face_ids_;
};
#endif // GEOMETRY_SELECTOR_HIGHLIGHT_H
