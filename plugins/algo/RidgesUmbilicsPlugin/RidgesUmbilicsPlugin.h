/**
 * @file RidgesUmbilicsPlugin.h
 * @brief CGAL 脊线与脐点分析插件入口，将 RidgesUmbilicsHandler 注册到算法系统
 * @date 2026-09-13
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "RidgesUmbilicsHandler.h"

#include <QObject>

namespace systems::algo {
class RidgesUmbilicsPlugin : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.algo.RidgesUmbilicsPlugin/1.0" FILE "RidgesUmbilicsPlugin.json")
private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override final
    {
        return HandlerCreatorDestroyerFactory<RidgesUmbilicsHandler, AlgorithmHandler>::get();
    }
};
}
