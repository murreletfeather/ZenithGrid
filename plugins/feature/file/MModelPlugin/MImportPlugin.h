/**
 * @file MImportPlugin.h
 * @brief M 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "MImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class MImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.MImportPlugin/1.0" FILE "MImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<MImportHandler, FeatureHandler>::get();
    }
};
}
