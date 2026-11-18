/**
 * @file FeatureIOSystemRegister.h
 * @brief 将旧 IO 插件注册为文件功能条目，复用原有编解码处理器。
 */
#pragma once
#include "SystemRegisterBase.h"
#include <string_view>

namespace systems::feature {
class FeatureSystem;

/** @brief 旧 IO 插件的迁移注册器；功能系统及其借用的 IO 系统须覆盖本对象生命周期。 */
class FeatureIOSystemRegister : public SystemRegisterBase {
public:
    explicit FeatureIOSystemRegister(FeatureSystem& system);
    /** @brief 判断文件功能是否由旧 IO 注册器桥接。 */
    static bool isLegacyFileFeature(std::string_view name);
    bool registerPlugin(const QJsonObject& meta_data, PluginBase& plugin) override;
    void unregisterPlugin(const QJsonObject& meta_data) override;

private:
    class FileFeature;
    FeatureSystem* system_;
};
}
