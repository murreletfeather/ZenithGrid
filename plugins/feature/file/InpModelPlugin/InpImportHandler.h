/**
 * @file InpImportHandler.h
 * @brief Inp 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "InpModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class InpImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "inp" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "Abaqus Input File", [](const std::filesystem::path& path) {
            return io::InpModelHandler().readModel(path);
        });
    }
};
}
