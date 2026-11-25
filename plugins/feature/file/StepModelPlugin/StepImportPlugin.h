/**
 * @file StepImportPlugin.h
 * @brief Step 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "StepImportHandler.h"
#include <QObject>

namespace systems::feature {
class StepImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.StepImportPlugin/1.0" FILE "StepImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<StepImportHandler, FeatureHandler>::get();
    }
};
}
