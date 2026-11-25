/**
 * @file PlyExportHandler.h
 * @brief Ply 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "PlyModelHandler.h"

namespace systems::feature {
class PlyExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "ply" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Stanford PLY file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::PlyModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
