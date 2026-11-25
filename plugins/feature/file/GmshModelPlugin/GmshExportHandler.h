/**
 * @file GmshExportHandler.h
 * @brief Gmsh 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "GmshModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class GmshExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "msh" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Gmsh mesh file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::GmshModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
