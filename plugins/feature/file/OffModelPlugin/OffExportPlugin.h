/**
 * @file OffExportPlugin.h
 * @brief Off 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "OffExportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class OffExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.OffExportPlugin/1.0" FILE "OffExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<OffExportHandler, FeatureHandler>::get();
    }
};
}
