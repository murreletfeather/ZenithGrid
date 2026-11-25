/**
 * @file StepExportHandler.h
 * @brief Step 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "StepModelHandler.h"

namespace systems::feature {
class StepExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "stp", "step", "stpnc", "p21", "210" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "STEP. ISO 10303",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::StepModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
