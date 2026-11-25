/**
 * @file StepExportPlugin.h
 * @brief Step 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "StepExportHandler.h"
#include <QObject>

namespace systems::feature {
class StepExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.StepExportPlugin/1.0" FILE "StepExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<StepExportHandler, FeatureHandler>::get();
    }
};
}
