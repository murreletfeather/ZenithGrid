/**
 * @file PlyImportHandler.h
 * @brief Ply 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "PlyModelHandler.h"

namespace systems::feature {
class PlyImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "ply" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "Stanford PLY file", [](const std::filesystem::path& path) {
            return io::PlyModelHandler().readModel(path);
        });
    }
};
}
