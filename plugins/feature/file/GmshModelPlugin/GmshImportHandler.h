/**
 * @file GmshImportHandler.h
 * @brief Gmsh 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "GmshModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class GmshImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "msh" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "Gmsh mesh file", [](const std::filesystem::path& path) {
            return io::GmshModelHandler().readModel(path);
        });
    }
};
}
