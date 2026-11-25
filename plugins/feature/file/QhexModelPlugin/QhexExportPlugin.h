/**
 * @file QhexExportPlugin.h
 * @brief Qhex 导出 Feature 插件入口。
 */
#pragma once
#include "HandlerCreatorDestroyerFactory.h"
#include "PluginBase.h"
#include "QhexExportHandler.h"
#include <QObject>

namespace systems::feature {
class QhexExportPlugin final : public QObject, public PluginBase {
    Q_OBJECT
    Q_INTERFACES(systems::PluginBase)
    Q_PLUGIN_METADATA(IID "com.PreCess.systems.feature.QhexExportPlugin/1.0" FILE "QhexExportPlugin.json")

private:
    const HandlerCreatorDestroyer& getHandlerCreatorDestroyer() noexcept override
    {
        return HandlerCreatorDestroyerFactory<QhexExportHandler, FeatureHandler>::get();
    }
};
}
