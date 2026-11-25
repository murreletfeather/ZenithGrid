/**
 * @file StlImportHandler.h
 * @brief Stl 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "StlModelHandler.h"

namespace systems::feature {
class StlImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "stl" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "STL stereolithography triangle mesh file", [](const std::filesystem::path& path) {
            return io::StlModelHandler().readModel(path);
        });
    }
};
}
