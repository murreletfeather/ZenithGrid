/**
 * @file InpImportPlugin.h
 * @brief Inp 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "InpImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class InpImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.InpImportPlugin/1.0" FILE "InpImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<InpImportHandler, FeatureHandler>::get();
    }
};
}
