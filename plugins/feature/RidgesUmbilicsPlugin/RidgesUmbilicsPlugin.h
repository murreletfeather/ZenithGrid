/**
 * @file RidgesUmbilicsPlugin.h
 * @brief CGAL 脊线与脐点分析 Feature 插件入口，将 RidgesUmbilicsHandler 注册到功能系统
 *
 * 基于 CGAL Ridges_3（Cazals & Pouget 脊线/脐点近似算法）对三角网格做曲率特征分析：
 * 逐顶点 Monge jet 拟合（Jet_fitting_3，SVD 由 Eigen 提供）后提取脊线与脐点，
 * 结果按类型落成新建几何组件（Ridge_* / Umbilic_*），不修改原网格。
 * 通过 Component 选择器参数显式指定目标 Component，避免依赖对象树选中态。
 * @date 2026-09-13
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "RidgesUmbilicsHandler.h"

#include <QObject>

namespace systems::feature {

/**
 * @brief 脊线与脐点分析 Feature 插件入口
 */
class RidgesUmbilicsPlugin : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.RidgesUmbilicsPlugin/1.0" FILE "RidgesUmbilicsPlugin.json")
private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override final
    {
        return HandlerCreatorDestroyerFactory<RidgesUmbilicsHandler, FeatureHandler>::get();
    }
};

} // namespace systems::feature
