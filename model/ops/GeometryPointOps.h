/** @file GeometryPointOps.h
 * @brief 精确边位置在相邻面上的只读映射，供拾取遮挡校验复用。
 */
#pragma once
#include "GeometryPoint.h"
#include <optional>
class TopoDS_Edge;
class TopoDS_Face;

/** @brief 保留原几何容差的参数查询，所有操作均不修改输入拓扑。 */
class GeometryPointOps {
public:
    /** @brief 从边参数求相邻面的坐标和 UV，不携带选择身份；非邻面或超差时返回空。 */
    static std::optional<GeometryPoint> projectEdgePointOnFace(
        const TopoDS_Face& face, const TopoDS_Edge& edge, double edge_parameter);
};
