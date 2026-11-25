/**
 * @file MeshMeditImportPlugin.h
 * @brief MeshMedit 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "MeshMeditImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class MeshMeditImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.MeshMeditImportPlugin/1.0" FILE "MeshMeditImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<MeshMeditImportHandler, FeatureHandler>::get();
    }
};
}
