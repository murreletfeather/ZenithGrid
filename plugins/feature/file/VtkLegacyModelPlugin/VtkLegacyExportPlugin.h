/**
 * @file VtkLegacyExportPlugin.h
 * @brief VtkLegacy 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "VtkLegacyExportHandler.h"
#include <QObject>

namespace systems::feature {
class VtkLegacyExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.VtkLegacyExportPlugin/1.0" FILE "VtkLegacyExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<VtkLegacyExportHandler, FeatureHandler>::get();
    }
};
}
