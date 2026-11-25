/**
 * @file GmshImportPlugin.h
 * @brief Gmsh 导入 Feature 插件入口。
 */
#pragma once
#include "GmshImportHandler.h"
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class GmshImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.GmshImportPlugin/1.0" FILE "GmshImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<GmshImportHandler, FeatureHandler>::get();
    }
};
}
