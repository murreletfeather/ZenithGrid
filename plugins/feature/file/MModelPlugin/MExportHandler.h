/**
 * @file MExportHandler.h
 * @brief M 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "MModelHandler.h"
#include "ModelFileFeatures.h"

namespace systems::feature {
class MExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "m" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Mesh .m file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::MModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
