/**
 * @file PlyImportPlugin.h
 * @brief Ply 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "PlyImportHandler.h"
#include <QObject>

namespace systems::feature {
class PlyImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.PlyImportPlugin/1.0" FILE "PlyImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<PlyImportHandler, FeatureHandler>::get();
    }
};
}
