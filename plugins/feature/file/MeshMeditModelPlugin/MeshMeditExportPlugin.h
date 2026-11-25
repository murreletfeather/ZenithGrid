/**
 * @file MeshMeditExportPlugin.h
 * @brief MeshMedit 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "MeshMeditExportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class MeshMeditExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.MeshMeditExportPlugin/1.0" FILE "MeshMeditExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<MeshMeditExportHandler, FeatureHandler>::get();
    }
};
}
