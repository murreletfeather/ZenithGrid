/**
 * @file VtkXmlImportHandler.h
 * @brief VtkXml 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "VtkXmlModelHandler.h"

namespace systems::feature {
class VtkXmlImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "vtp", "vtu" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "VTK XML mesh file", [](const std::filesystem::path& path) {
            return io::VtkXmlModelHandler().readModel(path);
        });
    }
};
}
