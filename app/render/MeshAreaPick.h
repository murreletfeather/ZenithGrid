// SPDX-License-Identifier: LGPL-3.0-only
/**
 * @file MeshAreaPick.h
 * @brief 网格框选的拾取与隔离工具
 *
 * vtkHardwareSelector 没有 pick list，会拾取当前可见的全部 actor。为只拿目标 actor
 * 自身的单元，拾取时临时把其它 actor VisibilityOff，再按返回节点 PROP 过滤，返回各
 * 目标 actor 的 render id（局部 cell/point id），由各 selector 自行反查为模型 id。
 * 一次可对多个 target actor 拾取：它们保持可见并互为 z-buffer，可见性决定哪些
 * target 实际被渲染拾取（隐藏组件不渲染 → 不产生命中）。
 *
 * PreCess 的 Transparent* 渲染模式会让 actor 半透明并关闭深度写入，拾取易误命中背面
 * 元素；拾取期间把 target 的 opacity 临时置 1.0 规避（RAII 见 MeshAreaPick.cpp 内
 * PickingOpacityGuard）。
 */
#ifndef MESH_AREA_PICK_H
#define MESH_AREA_PICK_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vtkPlanes.h>
#include <vtkSmartPointer.h>
#include <vtkType.h>

class vtkRenderer;
class vtkActor;
class vtkProp;
class vtkDataSet;
class vtkMatrix4x4;

namespace area_pick {

//! @brief 在指定屏幕矩形内同时对多个 target actor 执行一次硬件拾取，按 actor(PROP) 分组返回
//! @param renderer       渲染器
//! @param target_actors  拾取目标 actor 列表（其它 actor 临时隐藏；列表内保持可见并互为 z-buffer）
//! @param xmin ymin xmax ymax  屏幕像素坐标矩形（一般由 interactor 给出）
//! @param field_association vtkDataObject::FIELD_ASSOCIATION_POINTS / _CELLS
//! @return actor(PROP) -> 命中单元 id 集合（render id，与对应 actor 的 cell/point id 空间一致）
std::unordered_map<vtkProp*, std::unordered_set<vtkIdType>> executeAreaPicks(
    vtkRenderer* renderer,
    const std::vector<vtkActor*>& target_actors,
    int xmin, int ymin, int xmax, int ymax,
    int field_association);

//! @brief 穿透框选：把屏幕矩形反投影成相机视锥，用 vtkExtractSelectedFrustum 对每个
//!        target actor 的 polydata 直接做几何求交。
//!        与 executeAreaPicks（硬件拾取）的根本差别：硬件拾取走深度测试，每像素只留最前
//!        一个 id，被遮挡的背面单元不产生命中；本函数只判"投影是否落在矩形内"，不做遮挡
//!        判定，因此框内前层与背面单元一并命中（跨组件同样穿透）。
//!        穿透与非穿透两条路径语义分离，由调用方用参数选择。
//! @param renderer       渲染器（用于屏幕→世界反投影）
//! @param target_actors  参与拾取的 actor 列表（隐藏组件的 actor 由调用方排除）
//! @param xmin ymin xmax ymax  屏幕像素坐标矩形
//! @return actor(PROP) -> 命中 cell id 集合（局部 cell id，与 executeAreaPicks 的 id 空间一致，
//!         可直接喂给各 selector 的 selectArea）
std::unordered_map<vtkProp*, std::unordered_set<vtkIdType>> executePenetratingAreaPicks(
    vtkRenderer* renderer,
    const std::vector<vtkActor*>& target_actors,
    int xmin, int ymin, int xmax, int ymax);

//! @brief 构建屏幕矩形对应的世界空间视锥（6 面 vtkPlanes）
//!        与 VTK 的 vtkInteractorStyleRubberBandPick 同一套构造逻辑，保证视锥与屏幕矩形严格一致。
//!        返回的对象可复用于多个数据集的求交，避免每个数据集都重建一次（AreaPick 有开销）。
//! @return 视锥；rectangle 退化或拾取器不可用时返回 nullptr
vtkSmartPointer<vtkPlanes> buildSelectionFrustum(vtkRenderer* renderer,
    int xmin, int ymin, int xmax, int ymax);

//! @brief 用世界空间视锥对任意数据集做穿透求交，返回命中的**输入** cell id 集合
//!        穿透 = 只判"投影是否落在矩形内"，不做深度遮挡判定，故被遮挡的内部单元一并命中。
//! @param world_frustum  buildSelectionFrustum 得到的世界视锥（为空则直接返回空集）
//! @param data           参与求交的数据集（可为 actor 的 polydata，也可为体网格等无 actor 的数据）
//! @param local_to_world 数据集所在空间 → 世界的变换矩阵（可为空，表示两者相同）
//! @return 命中的输入 cell id 集合（已由输出的 vtkOriginalCellIds 还原）
std::unordered_set<vtkIdType> penetratingPickCells(vtkPlanes* world_frustum,
    vtkDataSet* data, vtkMatrix4x4* local_to_world);

//! @brief 数据空间 (lx,ly,lz) → world → display，测试屏幕投影是否落在框选矩形内。
//!        用于 face CELLS 拾取后的二次过滤，避免"面只一角进框、所有点/边都高亮"。
bool isWorldPointInScreenBox(vtkRenderer* renderer,
    vtkActor* transform_actor,
    double lx, double ly, double lz,
    int xmin, int ymin, int xmax, int ymax);

//! @brief 线段（world→display 投影后）是否与框选矩形相交。
//!        任一端点在框内、或线段穿过矩形（含擦边）均视为相交（"与边相交即选中"语义）。
bool isScreenSegmentIntersectsBox(vtkRenderer* renderer,
    vtkActor* transform_actor,
    double lx0, double ly0, double lz0,
    double lx1, double ly1, double lz1,
    int xmin, int ymin, int xmax, int ymax);

} // namespace area_pick

#endif // MESH_AREA_PICK_H
