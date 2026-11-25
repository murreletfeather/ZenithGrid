/**
 * @file PlyExportPlugin.h
 * @brief Ply 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "PlyExportHandler.h"
#include <QObject>

namespace systems::feature {
class PlyExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.PlyExportPlugin/1.0" FILE "PlyExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<PlyExportHandler, FeatureHandler>::get();
    }
};
}
