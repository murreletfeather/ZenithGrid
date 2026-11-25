/**
 * @file IgesExportPlugin.h
 * @brief Iges 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "IgesExportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class IgesExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.IgesExportPlugin/1.0" FILE "IgesExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<IgesExportHandler, FeatureHandler>::get();
    }
};
}
