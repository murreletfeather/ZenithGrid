/**
 * @file FeatureParams.h
 * @brief 功能的持久参数集
 */
#ifndef FEATURE_PARAMS_H
#define FEATURE_PARAMS_H
#include "ArgObject.h"
#include "ArgType.h"

#include <cstddef>
#include <vector>

namespace systems::feature {
/**
 * @brief 功能的持久参数集：参数类型与当前值一一对应
 *
 * 与算法系统的一次性传参不同，功能参数在功能注册后长期存在，
 * 初始值取自 ArgType::content，修改经 FeatureSystem::setParameter 生效并广播事件。
 */
class FeatureParams {
public:
    explicit FeatureParams(std::vector<core::ArgType> types);
    /** @brief 复制参数快照，选择值独立持有，避免重注册恢复时共享旧实例的可变选择。 */
    FeatureParams(const FeatureParams& other);
    /** @brief 用独立参数快照替换当前参数集。 */
    FeatureParams& operator=(const FeatureParams& other);
    /** @brief 转移已独立持有的参数，不重复复制选择值。 */
    FeatureParams(FeatureParams&&) noexcept = default;
    /** @brief 转移已独立持有的参数集。 */
    FeatureParams& operator=(FeatureParams&&) noexcept = default;

    const std::vector<core::ArgType>& types() const noexcept { return types_; }
    std::size_t count() const noexcept { return types_.size(); }
    /**
     * @brief 读取参数当前值
     * @throw std::out_of_range 下标越界
     */
    const core::ArgObject& value(std::size_t index) const;
    /**
     * @brief 设置参数当前值；Selector 保存独立选择值，不借用调用方的可变对象
     * @throw std::out_of_range 下标越界
     */
    void setValue(std::size_t index, core::ArgObject new_value);

private:
    std::vector<core::ArgType> types_;
    std::vector<core::ArgObject> values_;
};
}
#endif // FEATURE_PARAMS_H
