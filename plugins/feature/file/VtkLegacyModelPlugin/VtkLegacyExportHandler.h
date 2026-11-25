/**
 * @file VtkLegacyExportHandler.h
 * @brief VtkLegacy 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "VtkLegacyModelHandler.h"

namespace systems::feature {
class VtkLegacyExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "vtk" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "VTK Simple Legacy Formats",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::VtkLegacyModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
