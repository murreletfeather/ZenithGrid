/**
 * @file InpExportHandler.h
 * @brief Inp 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "InpModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class InpExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "inp" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Abaqus Input File",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::InpModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
