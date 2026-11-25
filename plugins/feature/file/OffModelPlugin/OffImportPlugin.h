/**
 * @file OffImportPlugin.h
 * @brief Off 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "OffImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class OffImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.OffImportPlugin/1.0" FILE "OffImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<OffImportHandler, FeatureHandler>::get();
    }
};
}
