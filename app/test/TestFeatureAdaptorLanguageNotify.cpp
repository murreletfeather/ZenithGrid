#include "EventBus.h"
#include "FeatureSystem.h"
#include "ModelLayer.h"
#include "QFeatureSystemAdaptor.h"

#include <QObject>

#include <catch2/catch_test_macros.hpp>

using systems::feature::QFeatureSystemAdaptor;

/**
 * @brief 语言切换刷新回归：Adaptor 重发 featuresInfoChanged，驱动 QML 按新语言重建信息列表
 *
 * 该行为位于 app 层（Adaptor），语言切换由 QModelManager::setLanguage 调
 * notifyLanguageChanged 触发；模型层不感知界面语言。
 */
TEST_CASE("QFeatureSystemAdaptor::notifyLanguageChanged re-emits featuresInfoChanged", "[app][language]")
{
    core::EventBus bus;
    ModelLayer model_layer;
    systems::feature::FeatureSystem system(model_layer, bus);
    QFeatureSystemAdaptor adaptor(system);

    int changed_count = 0;
    QObject::connect(&adaptor, &QFeatureSystemAdaptor::featuresInfoChanged, [&] { ++changed_count; });
    REQUIRE(changed_count == 0);

    adaptor.notifyLanguageChanged();
    REQUIRE(changed_count == 1);

    adaptor.notifyLanguageChanged();
    REQUIRE(changed_count == 2);
}
