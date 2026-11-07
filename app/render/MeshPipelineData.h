/**
 * @file MeshPipelineData.h
 * @brief GUI 线程持有的网格基础数据、面隐藏及裁剪管线。
 */
#pragma once

#include "Core.h"
#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include <vtkSmartPointer.h>
#include <vtkType.h>

struct MeshData;
class vtkPlane;
class vtkPolyData;
class vtkUnstructuredGrid;

/** @brief 跨线程传递的裁剪参数，不共享可变 vtkPlane。 */
struct MeshClipPlane {
    bool enabled { false };
    std::array<double, 3> origin { };
    std::array<double, 3> normal { 1.0, 0.0, 0.0 };
};

/**
 * @brief 已完成计算的网格快照；发布后不再改写，数组由源数据或 VTK 自持。
 *
 * 数据集包装在接收侧另建，底层数组只读共享；源网格同时保活诊断、属性用的视图。
 */
struct MeshDataSnapshot {
    std::uint64_t generation { };
    std::uint64_t revision { };
    bool visible { true };
    std::set<int> hidden_face_types;
    std::set<int> hidden_solid_types;
    MeshClipPlane clip;
    std::shared_ptr<const MeshData> source;
    std::shared_ptr<const MeshDataVtk> model_data;
    vtkSmartPointer<vtkPolyData> original_faces;
    vtkSmartPointer<vtkPolyData> original_edges;
    vtkSmartPointer<vtkUnstructuredGrid> original_solids;
    vtkSmartPointer<vtkPolyData> visible_faces;
    vtkSmartPointer<vtkPolyData> visible_edges;
    vtkSmartPointer<vtkPolyData> solid_surface;
    vtkSmartPointer<vtkPolyData> solid_internal_edges;
};

/** @brief 在创建线程串行修改网格数据，主动计算后向渲染端发布轻量快照。 */
class MeshPipelineData {
public:
    /** @brief 绑定创建线程，所有后续计算也必须在该线程执行。 */
    MeshPipelineData();
    ~MeshPipelineData();

    /** @brief 捕获自持的原始网格并重置旧面隐藏，保留整体开关与裁剪参数。 */
    void loadModelData(const MeshDataVtk& data);
    /** @brief 更新整体网格开关，不改变隐藏集合。 */
    void setVisibility(bool visible);
    /** @brief 设置面/体类型开关；type=-1 批量控制当前维度，保留人工隐藏和裁剪。 */
    void setCellTypeVisibility(int dimension, int type, bool visible);
    /** @brief 恢复所有单元类型，不改变手动面隐藏集合。 */
    void showAllCellTypes();
    /** @brief 查询隐藏类型，供界面同步与同组件网格更新时沿用。 */
    const std::set<int>& hiddenCellTypes(int dimension) const;
    /** @brief 查询数据侧整体网格开关。 */
    bool isVisibilityEnabled() const { return visible_; }
    /** @brief 判断开关、类型过滤和人工隐藏之后是否仍有可见单元，不计裁剪与渲染样式。 */
    bool isVisible() const;
    /** @brief 隐藏有效的原始面 ID，累积并去重。 */
    void hideFaces(const std::vector<vtkIdType>& face_ids);
    /** @brief 恢复指定原始面；整体关闭时仅显示指定面。 */
    void showFaces(const std::vector<vtkIdType>& face_ids);
    /** @brief 仅保留指定原始面，不改变同组件其他渲染对象的样式。 */
    void isolateFaces(const std::vector<vtkIdType>& face_ids);
    /** @brief 恢复全部人工隐藏面并结束面隔离。 */
    void showAllFaces();
    /** @brief 反转局部面/体过滤；整体关闭时恢复全部单元，保留裁剪并返回整体开关。 */
    bool reverseVisibility();
    /** @brief 复制调用线程中的裁剪平面参数，nullptr 表示停用。 */
    void setClipPlane(vtkPlane* plane);
    /** @brief 应用来自渲染控件的裁剪值快照。 */
    void setClipPlane(const MeshClipPlane& plane);
    /** @brief 主动计算需要更新的数据；每次发布递增版本，绝不重新执行旧输出管线。 */
    std::shared_ptr<const MeshDataSnapshot> createSnapshot();
    /** @brief 返回原始网格代次，用于丢弃旧选择消息。 */
    std::uint64_t generation() const { return generation_; }

private:
    /** @brief 防止 GUI 管线意外被渲染线程调用。 */
    void assertOwnerThread() const;
    /** @brief 重建独立的计算实例，旧快照所引用的输出不再被修改。 */
    void rebuildOutputs();

    std::thread::id owner_thread_;
    std::shared_ptr<const MeshData> source_;
    std::shared_ptr<const MeshDataVtk> model_data_;
    vtkSmartPointer<vtkPolyData> face_data_;
    vtkSmartPointer<vtkPolyData> edge_data_;
    vtkSmartPointer<vtkUnstructuredGrid> solid_data_;
    std::shared_ptr<MeshDataSnapshot> prepared_;
    std::vector<vtkIdType> hidden_face_ids_;
    std::set<int> hidden_face_types_;
    std::set<int> hidden_solid_types_;
    MeshClipPlane clip_;
    bool visible_ { true };
    bool dirty_ { true };
    std::uint64_t generation_ { };
    std::uint64_t revision_ { };
};
