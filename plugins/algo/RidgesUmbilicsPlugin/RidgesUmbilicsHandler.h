/**
 * @file RidgesUmbilicsHandler.h
 * @brief 基于 CGAL Ridges_3 的脊线与脐点近似分析处理器声明
 * @date 2026-09-13
 */
#pragma once
#include "AlgorithmHandler.h"
#include <any>
#include <optional>
#include <vector>

namespace systems::algo {
/**
 * @brief 基于 CGAL Ridges_3 的三角网格脊线（ridge lines）与脐点（umbilics）近似分析
 *
 * 经选择器解析目标三角网格 Component（覆盖 @c AlgorithmHandler::resolveComponentId，
 * 选择器优先、对象树选中态兜底），逐顶点 Monge jet 拟合主曲率/主方向与三、四阶系数后，
 * 调用 CGAL Ridge_approximation / Umbilic_approximation 提取脊线与脐点。
 * 结果按类型落成新建几何组件（OCC 折线边/点），不修改原网格；undo 由算法系统边界自动记录。
 */
class RidgesUmbilicsHandler : public AlgorithmHandler {
public:
    RidgesUmbilicsHandler() = default;
    ~RidgesUmbilicsHandler() override = default;

    std::optional<Index> resolveComponentId(
        ModelLayer& model_layer,
        Index fallback_component_id,
        const std::vector<core::ArgObject>& args) const override;
    std::any execute(HandlerContext& context, const std::vector<core::ArgObject>& args) override;
    std::vector<core::ArgType> args_type() const override;
};
}
