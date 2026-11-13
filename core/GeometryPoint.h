/**
 * @file GeometryPoint.h
 * @brief 精确几何点数据：保留支撑拓扑和精确参数，无界面依赖。
 */
#pragma once

#include "Core.h"

#include <array>

/** @brief 几何点依附的拓扑类型。 */
enum class GeometryPointKind {
    None,
    Vertex,
    Edge,
    Face
};

/**
 * @brief 一个精确几何点；支撑对象决定操作目标，参数用于真实曲线或曲面求值。
 * @note 几何根替换或撤销后必须使点输入失效，不能跨拓扑变化继续使用旧身份。
 */
struct GeometryPoint {
    Index component_id { -1 };
    GeometryPointKind support_kind { GeometryPointKind::None };
    Index support_id { -1 };
    std::array<double, 3> world_pos { };
    double edge_parameter { };
    std::array<double, 2> face_uv { };
    bool valid { false };
};
