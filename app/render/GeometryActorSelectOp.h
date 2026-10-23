#ifndef GEOMETRY_ACTOR_SELECT_OP_H
#define GEOMETRY_ACTOR_SELECT_OP_H
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <vtkSmartPointer.h>
#include <vtkType.h>

#include "Core.h"
#include "GeometryActor.h"

class GeometryActorSelectOp;
class IVtkTools_ShapePicker;
class IVtkTools_SubPolyDataFilter;
class vtkActor;
class vtkRenderer;

class GeometryActorSelectOpFactory {
    friend GeometryActorSelectOp;

public:
    GeometryActorSelectOpFactory();
    GeometryActorSelectOpFactory(std::weak_ptr<GeometryActor> geometry_actor);
    std::optional<GeometryActorSelectOp> lock();

private:
    std::weak_ptr<GeometryActor> geometry_actor_;
};

class GeometryActorSelectOp {
    friend GeometryActorSelectOpFactory;

public:
    GeometryActorSelectOp(std::shared_ptr<GeometryActor> geometry_actor);

    static int toleranceForMode(SelectMode m);

    IVtk_IdType getShapeId() const;
    void disableSelectionModes(IVtkTools_ShapePicker* picker) const;
    void enableSelectionMode(IVtkTools_ShapePicker* picker, SelectMode mode) const;

    std::optional<Index> resolvePickedSubshape(IVtkTools_ShapePicker* picker, IVtk_IdType shapeId,
        SelectMode mode, IVtk_IdType& out_sub_id) const;

    std::vector<std::pair<IVtk_IdType, Index>> resolvePickedSubshapes(
        IVtkTools_ShapePicker* picker, IVtk_IdType shapeId, SelectMode mode) const;

    /**
     * @brief 取几何顶点子形状的世界坐标
     * @param sub_id IVtk 子形状 id（由 SM_Vertex 模式拾取得到）
     * @return 顶点坐标；子形状无效或不是顶点时返回 std::nullopt
     */
    std::optional<std::array<double, 3>> vertexPoint(IVtk_IdType sub_id) const;

    bool resolvePickedSolid(IVtkTools_ShapePicker* picker, IVtk_IdType shapeId,
        GeomSolidId& out_solid_id, std::vector<IVtk_IdType>& out_face_sub_ids) const;

    /**
     * @brief 由子形状 id 解析其所属 solid（非穿透框选用，不依赖 picker）
     * @param sub_id 子形状 id——通常是硬件拾取给出的可见面
     * @return 找到所属 solid 时返回 true，并回填 solid id 与其所有面的子形状 id
     */
    bool resolveSolidOfSubshape(IVtk_IdType sub_id,
        GeomSolidId& out_solid_id, std::vector<IVtk_IdType>& out_face_sub_ids) const;

    vtkSmartPointer<IVtkTools_SubPolyDataFilter> buildHighlight(SelectMode mode);

    /**
     * @brief 渲染 cell id → IVtk 子形状 id（非穿透框选用）
     * @param cell_id 几何 poly actor 所渲染 polydata 的 cell 下标
     * @return 子形状 id；cell 越界或缺少 SUBSHAPE_IDS 数组时返回 std::nullopt
     * @note 该数组由 OCC IVtk 写入（IVtkVTK_ShapeData::ARRNAME_SUBSHAPE_IDS），与渲染数据一一对应，因此无需另行建立映射。
     */
    std::optional<IVtk_IdType> subshapeIdOfCell(vtkIdType cell_id);

    /**
     * @brief 子形状 id → 几何元素 id（非穿透框选用）
     * @param sub_id IVtk 子形状 id
     * @param mode   当前选择模式，决定按面 / 边 / 顶点解析
     * @return 几何元素 id；该子形状不属于当前模式对应的类型时返回 std::nullopt
     */
    std::optional<Index> resolveSubshapeGeomId(IVtk_IdType sub_id, SelectMode mode) const;

    vtkActor& getPolyActor();
    bool isVisible() const;

private:
    std::shared_ptr<GeometryActor> geometry_actor_;
};
#endif // GEOMETRY_ACTOR_SELECT_OP_H
