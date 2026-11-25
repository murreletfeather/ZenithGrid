/**
 * @file GmshExportPlugin.h
 * @brief Gmsh 导出 Feature 插件入口。
 */
#pragma once
#include "GmshExportHandler.h"
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class GmshExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.GmshExportPlugin/1.0" FILE "GmshExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<GmshExportHandler, FeatureHandler>::get();
    }
};
}
