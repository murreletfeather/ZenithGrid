/**
 * @file IgesImportHandler.h
 * @brief Iges 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "IgesModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class IgesImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "igs", "iges" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "IGES", [](const std::filesystem::path& path) {
            return io::IgesModelHandler().readModel(path);
        });
    }
};
}
