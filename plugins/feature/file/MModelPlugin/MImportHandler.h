/**
 * @file MImportHandler.h
 * @brief M 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "MModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class MImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "m" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "Mesh .m file", [](const std::filesystem::path& path) {
            return io::MModelHandler().readModel(path);
        });
    }
};
}
