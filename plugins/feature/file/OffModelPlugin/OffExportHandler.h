/**
 * @file OffExportHandler.h
 * @brief Off 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "OffModelHandler.h"

namespace systems::feature {
class OffExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "off" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "OFF file format",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::OffModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
