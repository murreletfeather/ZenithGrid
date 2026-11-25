/**
 * @file OBJImportPlugin.h
 * @brief OBJ 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "OBJImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class OBJImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.OBJImportPlugin/1.0" FILE "OBJImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<OBJImportHandler, FeatureHandler>::get();
    }
};
}
