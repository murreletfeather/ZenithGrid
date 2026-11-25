/**
 * @file MExportPlugin.h
 * @brief M 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "MExportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class MExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.MExportPlugin/1.0" FILE "MExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<MExportHandler, FeatureHandler>::get();
    }
};
}
