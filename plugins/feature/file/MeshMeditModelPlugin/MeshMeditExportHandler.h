/**
 * @file MeshMeditExportHandler.h
 * @brief MeshMedit 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "MeshMeditModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class MeshMeditExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "mesh", "meshb" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Gamma Mesh Format @Medit",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::MeshMeditModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
