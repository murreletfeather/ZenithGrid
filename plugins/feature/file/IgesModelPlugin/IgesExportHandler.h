/**
 * @file IgesExportHandler.h
 * @brief Iges 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "IgesModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class IgesExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "igs", "iges" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "IGES",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::IgesModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
