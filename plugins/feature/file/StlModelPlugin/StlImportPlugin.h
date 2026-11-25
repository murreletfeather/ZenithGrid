/**
 * @file StlImportPlugin.h
 * @brief Stl 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "StlImportHandler.h"
#include <QObject>

namespace systems::feature {
class StlImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.StlImportPlugin/1.0" FILE "StlImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<StlImportHandler, FeatureHandler>::get();
    }
};
}
