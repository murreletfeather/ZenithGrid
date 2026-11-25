/**
 * @file IgesImportPlugin.h
 * @brief Iges 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "IgesImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class IgesImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.IgesImportPlugin/1.0" FILE "IgesImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<IgesImportHandler, FeatureHandler>::get();
    }
};
}
