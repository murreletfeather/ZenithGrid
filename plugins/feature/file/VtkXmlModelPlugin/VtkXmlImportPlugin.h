/**
 * @file VtkXmlImportPlugin.h
 * @brief VtkXml 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "VtkXmlImportHandler.h"
#include <QObject>

namespace systems::feature {
class VtkXmlImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.VtkXmlImportPlugin/1.0" FILE "VtkXmlImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<VtkXmlImportHandler, FeatureHandler>::get();
    }
};
}
