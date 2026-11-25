/**
 * @file QhexImportHandler.h
 * @brief Qhex 导入 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "QhexModelHandler.h"

namespace systems::feature {
class QhexImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "qhex" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::importModels(context, "Qhex hexahedral mesh file", [](const std::filesystem::path& path) {
            return io::QhexModelHandler().readModel(path);
        });
    }
};
}
