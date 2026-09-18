/**
 * @file RidgesUmbilicsHandler.h
 * @brief 基于 CGAL Ridges_3 的脊线与脐点近似分析功能处理器声明
 * @date 2026-09-13
 */
#pragma once
#include "FeatureHandler.h"

#include <any>

namespace systems::feature {
/**
 * @brief 基于 CGAL Ridges_3 的三角网格脊线（ridge lines）与脐点（umbilics）近似分析功能
 *
 * 经 Selector 参数解析目标三角网格 Component（AGENTS.md §10：不依赖对象树选中态），
 * 逐顶点 Monge jet 拟合主曲率/主方向与三、四阶系数后，调用 CGAL
 * Ridge_approximation / Umbilic_approximation 提取脊线与脐点。
 * 结果按类型落成新建几何组件（OCC 折线边/点），不修改原网格；
 * addGeometryComponent 结构操作随 FeatureSystem::invoke 边界即时成 undo 记录。
 */
class RidgesUmbilicsHandler : public FeatureHandler {
public:
    /**
     * @brief 注册目标 Component 选择器、分析参数与「功能/网格」菜单项
     */
    void setup(FeatureRegistrar& reg, FeatureContext& ctx) override;

    /**
     * @brief 执行脊线与脐点分析，结果组件写入目标组件所属模型，返回结果文案
     */
    std::any execute(FeatureContext& ctx) override;
};
}
