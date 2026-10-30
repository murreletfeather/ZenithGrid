/**
 * @file private_ElementEnum.h
 * @brief 原生与 QML 共用的完整选择类型声明；用于嵌入类体，不设 include guard。
 */
enum Type {
    None = 0,
    Solid,
    Face,
    Edge,
    Vertex,
    Block,
    Group,
    GeometrySolid,
    GeometryFace,
    GeometryEdge,
    GeometryVertex,
    Component,
    GeometryPoint
};
