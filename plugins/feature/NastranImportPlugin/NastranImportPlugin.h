#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "NastranImportHandler.h"
#include "PluginBase.h"
#include <QObject>

namespace systems::feature {
class NastranImportPlugin : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.NastranImportPlugin/1.0" FILE "NastranImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override final
    {
        return HandlerCreatorDestroyerFactory<NastranImportHandler, FeatureHandler>::get();
    }
};
}
