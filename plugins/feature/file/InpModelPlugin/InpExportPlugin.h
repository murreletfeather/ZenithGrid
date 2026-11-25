/**
 * @file InpExportPlugin.h
 * @brief Inp 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "InpExportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class InpExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.InpExportPlugin/1.0" FILE "InpExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<InpExportHandler, FeatureHandler>::get();
    }
};
}
