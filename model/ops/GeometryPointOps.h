/**
 * @file GeometryPointOps.h
 * @brief 与界面无关的精确几何点解算及贴面路径采样。
 */
#pragma once

#include "GeometryPoint.h"

#include <array>
#include <optional>
#include <vector>

class TopoDS_Edge;
class TopoDS_Face;

/** @brief 从几何曲线、曲面解算参数点，所有查询均保持输入拓扑不变。 */
class GeometryPointOps {
public:
    /** @brief 取射线与修剪面的最近正向交点；射线未命中时返回空。 */
    static std::optional<GeometryPoint> intersectFace(
        const TopoDS_Face& face, Index component_id, Index face_id,
        const std::array<double, 3>& origin, const std::array<double, 3>& direction);

    /** @brief 将世界坐标投影到修剪面，仅接受位于面内部或边界的投影。 */
    static std::optional<GeometryPoint> projectOnFace(
        const TopoDS_Face& face, Index component_id, Index face_id,
        const std::array<double, 3>& world_pos);

    /** @brief 求曲线有效参数区间内的最近点，包含两个端点。 */
    static std::optional<GeometryPoint> projectOnEdge(
        const TopoDS_Edge& edge, Index component_id, Index edge_id,
        const std::array<double, 3>& world_pos);

    /**
     * @brief 将边的三维参数映射到该边在目标面上的 pcurve，返回精确面点及边吸附身份。
     * @note 世界坐标严格来自面 UV；三维边与 pcurve 的偏差不得超过原边容差。
     *       不同参数域在限定的面上边曲线求最近对应，不投影到其他曲面分支；输入保持不变。
     */
    static std::optional<GeometryPoint> projectEdgePointOnFace(
        const TopoDS_Face& face, const TopoDS_Edge& edge, Index component_id,
        Index face_id, Index edge_id, double edge_parameter);

    /** @brief 将周期等价 UV 归回修剪域后判断边界，与贴面路径校验共用分类规则。 */
    static bool isOnFaceBoundary(const TopoDS_Face& face, const std::array<double, 2>& uv);

    /**
     * @brief 沿连续展开的 UV 折线采样支撑曲面，供已完成和未完成路径预览共用。
     * @note 单点返回该点；无效点或越出修剪域的路径抛异常。
     */
    static std::vector<std::array<double, 3>> sampleSurfacePath(
        const TopoDS_Face& face, const std::vector<GeometryPoint>& points);

    /** @brief 校验点与修剪域并连续展开周期 UV；正式构线与预览使用同一条参数路径。 */
    static std::vector<std::array<double, 2>> resolveSurfacePathParameters(
        const TopoDS_Face& face, const std::vector<GeometryPoint>& points);
};
