/**
 * @file QhexImportPlugin.h
 * @brief Qhex 导入 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "QhexImportHandler.h"
#include <QObject>

namespace systems::feature {
class QhexImportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.QhexImportPlugin/1.0" FILE "QhexImportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<QhexImportHandler, FeatureHandler>::get();
    }
};
}
