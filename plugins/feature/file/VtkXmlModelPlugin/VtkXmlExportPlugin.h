/**
 * @file VtkXmlExportPlugin.h
 * @brief VtkXml 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "VtkXmlExportHandler.h"
#include <QObject>

namespace systems::feature {
class VtkXmlExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.VtkXmlExportPlugin/1.0" FILE "VtkXmlExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<VtkXmlExportHandler, FeatureHandler>::get();
    }
};
}
