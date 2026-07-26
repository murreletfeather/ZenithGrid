#pragma once

#include "HandlerCreatorDestroyerFactory.h"
#include "MeshRepairHandler.h"
#include "PluginBase.h"

#include <QObject>

namespace systems::feature {

/**
 * @brief 网格修复 Feature 插件入口
 */
class MeshRepairPlugin : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.MeshRepairPlugin/1.0" FILE "MeshRepairPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override final
    {
        return HandlerCreatorDestroyerFactory<MeshRepairHandler, FeatureHandler>::get();
    }
};

}
