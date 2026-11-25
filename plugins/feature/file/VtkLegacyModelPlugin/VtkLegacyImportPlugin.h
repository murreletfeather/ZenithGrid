/**
 * @file VtkLegacyImportPlugin.h
 * @brief VtkLegacy 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "VtkLegacyImportHandler.h"
#include <QObject>

namespace systems::feature {
class VtkLegacyImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.VtkLegacyImportPlugin/1.0" FILE "VtkLegacyImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<VtkLegacyImportHandler, FeatureHandler>::get();
    }
};
}
