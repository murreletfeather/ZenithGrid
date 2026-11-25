/**
 * @file VtkLegacyImportHandler.h
 * @brief VtkLegacy 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "VtkLegacyModelHandler.h"

namespace systems::feature {
class VtkLegacyImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "vtk" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "VTK Simple Legacy Formats", [](const std::filesystem::path& path) {
            return io::VtkLegacyModelHandler().readModel(path);
        });
    }
};
}
