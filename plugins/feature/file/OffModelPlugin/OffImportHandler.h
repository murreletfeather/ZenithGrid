/**
 * @file OffImportHandler.h
 * @brief Off 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "OffModelHandler.h"

namespace systems::feature {
class OffImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "off" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "OFF file format", [](const std::filesystem::path& path) {
            return io::OffModelHandler().readModel(path);
        });
    }
};
}
