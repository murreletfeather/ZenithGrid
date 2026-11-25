/**
 * @file StepImportHandler.h
 * @brief Step 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "StepModelHandler.h"

namespace systems::feature {
class StepImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "stp", "step", "stpnc", "p21", "210" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "STEP. ISO 10303", [](const std::filesystem::path& path) {
            return io::StepModelHandler().readModel(path);
        });
    }
};
}
