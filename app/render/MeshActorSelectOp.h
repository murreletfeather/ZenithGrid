#ifndef MESH_ACTOR_SELECT_OP_H
#define MESH_ACTOR_SELECT_OP_H
#include <memory>
#include <optional>
#include <vtkSmartPointer.h>

class vtkIdTypeArray;
class vtkProp;
class vtkPolyData;
class vtkUnstructuredGrid;
class vtkExtractSelection;
class MeshActorSelectOp;
class MeshActor;

/**
 * @brief MeshActor在选择过程中的操作句柄类，可以在选择系统中保存该句柄，延迟获取操作对象
 */
class MeshActorSelectOpFactory {
    friend MeshActorSelectOp;

public:
    MeshActorSelectOpFactory();
    MeshActorSelectOpFactory(std::weak_ptr<MeshActor> mesh_actor);
    /**
     * @brief 获取操作对象
     * @return 如果MeshActor还存在，返回MeshActorSelectOp对象，否则返回std::nullopt
     */
    std::optional<MeshActorSelectOp> lock();

private:
    std::weak_ptr<MeshActor> mesh_actor_;
};

/**
 * @brief MeshActor在选择过程中的操作类，作为临时对象使用
 */
class MeshActorSelectOp {
    friend MeshActorSelectOpFactory;

public:
    MeshActorSelectOp(std::shared_ptr<MeshActor> mesh_actor);

    vtkProp& getSolidActor();
    vtkProp& getFaceActor();
    vtkProp& getEdgeActor();

    /**
     * @brief 体网格（体单元）数据集（未裁剪）
     *
     * cell id 即 solid id（该数据集的 vtkOriginalCellIds 为 identity），点 id 即组件内局部
     * 点 id（vtkOriginalPointIds 为 identity）。体网格**内部**的体单元与内部节点只能从这里
     * 选到——face/edge/solid actor 渲染的都是边界表示，不含内部单元。
     */
    vtkUnstructuredGrid* getSolidData();

    /**
     * @brief 遵守剖切条件的体网格（体单元）数据集，供穿透框选内部命中使用
     *
     * 无剖切时返回 getSolidData()（原始体网格，cell 下标即 solid id）；
     * 有剖切时返回 solid_clipper_ 的输出（已裁剪的体网格），其 CellData 携带 vtkOriginalCellIds，
     * 可将裁剪后的 cell 下标还原为原始模型体 id。调用方须对拾取结果做此还原。
     */
    vtkUnstructuredGrid* getClippedSolidData();

    /**
     * @brief 体网格的边数据集（遵守剖切条件的 vtkExtractEdges 输出），含体单元之间的**内部边**
     *
     * cell 为 VTK_LINE，点数据带 vtkOriginalPointIds（组件内局部点 id）。
     * Edge 模式靠它选中体网格内部边；内部边不在 MeshData::edge_vertices_ 里，没有模型边 id，
     * 因此派生出的边其 edge_id 为 -1，按端点对标识（已有兜底路径）。
     */
    vtkPolyData* getSolidEdgeData();

    bool isVisible() const;

    /**
     * @brief 根据体id，提取体数据
     * @param[in] ids 体id数组
     * @return 提取出的体数据 Filter
     */
    vtkSmartPointer<vtkExtractSelection> extractSolid(vtkIdTypeArray* ids);

    /**
     * @brief 根据点id，提取点数据
     * @param ids 组件内局部点 id 数组（VTK 点索引；选择列表为活引用，数组内容更新后随管线重取生效）
     * @return 提取出的点数据 Filter
     */
    vtkSmartPointer<vtkExtractSelection> extractVertex(vtkIdTypeArray* ids);

    /**
     * @brief 根据边的点id对，提取边数据
     * @param ids 每个元素表示一条边（组件内局部点 id 对）
     * @return 提取出的边数据 Filter
     */
    vtkSmartPointer<vtkPolyData> extractEdge(std::vector<std::array<vtkIdType, 2>>& ids);

private:
    std::shared_ptr<MeshActor> mesh_actor_; // 非空
};
#endif // MESH_ACTOR_SELECT_OP_H