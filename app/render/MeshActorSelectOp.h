#ifndef MESH_ACTOR_SELECT_OP_H
#define MESH_ACTOR_SELECT_OP_H
#include <memory>
#include <optional>
#include <vector>
#include <vtkSmartPointer.h>

class vtkIdTypeArray;
class vtkProp;
class vtkPolyData;
class vtkPoints;
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
     * @brief 组件公共点表（渲染端私有 vtkPoints），供穿透选点逐点做视锥判断
     *
     * 下标即组件原始点 id，与快照的 selectable_points 位图同一键空间；底层坐标只读共享。
     */
    vtkPoints* getPoints() const;

    /**
     * @brief 穿透选点的可选点身份图（按组件原始点下标索引，1 表示仍被保留单元引用）
     *
     * 来自数据层快照，与 getPoints() 配套使用；快照缺失时返回 nullptr。
     */
    const std::vector<unsigned char>* getSelectablePoints() const;

    /**
     * @brief 遵守裁剪条件的体网格（体单元）数据集，供穿透框选内部体单元使用
     *
     * 直接取用数据层快照中已发布的体裁剪结果（携带 vtkOriginalCellIds，可将裁剪后的
     * cell 下标还原为原始模型体 id）。快照已保证该字段非空（无过滤时复用原始体），调用方须对拾取结果做 ID 还原。
     */
    vtkUnstructuredGrid* getClippedSolidData() const;

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