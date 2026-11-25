/**
 * @file MeshMeditImportHandler.h
 * @brief MeshMedit 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "MeshMeditModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class MeshMeditImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "mesh", "meshb" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "Gamma Mesh Format @Medit", [](const std::filesystem::path& path) {
            return io::MeshMeditModelHandler().readModel(path);
        });
    }
};
}
