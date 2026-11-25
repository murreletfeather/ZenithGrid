/**
 * @file OBJExportHandler.h
 * @brief OBJ 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "OBJModelHandler.h"

namespace systems::feature {
class OBJExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "obj" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Wavefront .obj file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::OBJModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
