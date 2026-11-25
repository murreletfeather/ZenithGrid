/**
 * @file VtkXmlExportHandler.h
 * @brief VtkXml 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "VtkXmlModelHandler.h"

namespace systems::feature {
class VtkXmlExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "vtp", "vtu" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "VTK XML mesh file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::VtkXmlModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
