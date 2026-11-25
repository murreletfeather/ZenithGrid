/**
 * @file OBJExportPlugin.h
 * @brief OBJ 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "OBJExportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class OBJExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.OBJExportPlugin/1.0" FILE "OBJExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<OBJExportHandler, FeatureHandler>::get();
    }
};
}
