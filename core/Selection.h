/** @file Selection.h
 * @brief 拓扑集合与有序几何位置共用的类型化选择值。
 */
#ifndef SELECTION_H
#define SELECTION_H
#include "Core.h"
#include "GeometryPoint.h"
#include <any>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

class ElementEnum {
public:
    enum Type {
        None,
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
};

//! @brief 存储选择的对象
class Selection {
public:
    /** @brief 枚举与载荷的唯一映射：拓扑使用 ID 列表，几何位置使用有序精确点列。 */
    template <ElementEnum::Type T>
    using Value = std::conditional_t<T == ElementEnum::GeometryPoint, std::vector<GeometryPoint>, std::vector<Index>>;
    /** @brief 默认空拓扑选择，枚举和载荷始终同时初始化。 */
    Selection() = default;
    /** @brief 兼容既有拓扑选择构造，禁止用 ID 数组声明几何位置。 */
    Selection(std::vector<Index> ids, ElementEnum::Type type, Index component = -1)
        : component_id(component)
        , type_(type)
        , value_(std::move(ids))
    {
        if (type == ElementEnum::GeometryPoint)
            throw std::invalid_argument("GeometryPoint selection requires precise point values");
    }
    /** @brief 类型化构造把枚举与 std::any 载荷同时确定，调用者不能独立修改类型标记。 */
    template <ElementEnum::Type T>
    static Selection create(Value<T> value, Index component = -1)
    {
        Selection result;
        result.type_ = T;
        result.value_ = std::move(value);
        result.component_id = component;
        return result;
    }
    /** @brief 只在枚举和载荷均匹配时读取，错误类型返回空指针。 */
    template <ElementEnum::Type T>
    const Value<T>* get() const { return type_ == T ? std::any_cast<Value<T>>(&value_) : nullptr; }
    /** @brief 在类型约束内修改已有载荷，不暴露裸 std::any。 */
    template <ElementEnum::Type T>
    Value<T>* get() { return type_ == T ? std::any_cast<Value<T>>(&value_) : nullptr; }
    /** @brief 返回统一选择类型。 */
    ElementEnum::Type type() const { return type_; }
    /** @brief 既有拓扑构建器切换 ID 类型；跨载荷类型时同时清空旧值。 */
    void setType(ElementEnum::Type type)
    {
        if (type == ElementEnum::GeometryPoint && type_ != type)
            value_ = std::vector<GeometryPoint> { };
        else if (type != ElementEnum::GeometryPoint && type_ == ElementEnum::GeometryPoint)
            value_ = std::vector<Index> { };
        type_ = type;
    }
    /**
     * @brief 拓扑 ID 兼容访问；Vertex 为网格全局点 ID，Edge 为稳定局部边 ID，几何拓扑类型为全局 gid。
     * @throws std::logic_error 当前载荷不包含拓扑 ID 数组，例如 GeometryPoint 点列。
     * @note 几何点请通过 get<ElementEnum::GeometryPoint>() 读取，类型不匹配时返回空指针。
     */
    const std::vector<Index>& ids() const
    {
        const auto* ids = std::any_cast<std::vector<Index>>(&value_);
        if (!ids)
            throw std::logic_error("Selection does not contain topology IDs");
        return *ids;
    }
    /**
     * @brief 保留原拓扑集合构建流程，同时拒绝把 ID 写入精确点载荷。
     * @throws std::logic_error 当前载荷不包含拓扑 ID 数组，例如 GeometryPoint 点列。
     * @note 几何点请通过 get<ElementEnum::GeometryPoint>() 修改，类型不匹配时返回空指针。
     */
    std::vector<Index>& ids()
    {
        return const_cast<std::vector<Index>&>(std::as_const(*this).ids());
    }
    /** @brief 通用数量查询，不以列表长度猜测拾取来源或载荷类型。 */
    size_t size() const
    {
        if (const auto* points = get<ElementEnum::GeometryPoint>())
            return points->size();
        return ids().size();
    }
    Index component_id { -1 };

private:
    ElementEnum::Type type_ { ElementEnum::None };
    std::any value_ { std::vector<Index> { } };
};
#endif // SELECTION_H
