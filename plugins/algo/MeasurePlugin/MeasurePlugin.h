/**
 * @file MeasurePlugin.h
 * @brief 测量插件声明：将 MeasureHandler 注册到算法系统
 * @author 范成通 email 1941804585@qq.com
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "MeasureHandler.h"
#include "PluginBase.h"

#include <QObject>

namespace systems::algo {
class MeasurePlugin : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.algo.MeasurePlugin/1.0" FILE "MeasurePlugin.json")
private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override final
    {
        return HandlerCreatorDestroyerFactory<MeasureHandler, AlgorithmHandler>::get();
    }
};
}
