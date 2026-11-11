/** @file GeometryPointResolver.h
 * @brief 从渲染侧几何快照求边上的精确位置，无参数监听或显示职责。
 */
#pragma once
#include "GeometryPoint.h"
#include <memory>
class vtkRenderer;
class GeometryActorManagerSelectOp;

/** @brief 缓存可见性查询所需的边与面；几何替换后由宿主显式失效。 */
class GeometryPointResolver {
public:
    /** @brief 绑定渲染器和几何快照查询，首次拾取时准备候选。 */
    GeometryPointResolver(vtkRenderer& renderer, GeometryActorManagerSelectOp& op);
    ~GeometryPointResolver();
    /** @brief 释放旧几何候选，下一次拾取按新场景重新准备。 */
    void clear();
    /** @brief 在屏幕半径内求最近可见边位置，坐标与参数均来自真实曲线。 */
    GeometryPoint resolvePoint(double x, double y, double pixel_ratio = 1.0) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
