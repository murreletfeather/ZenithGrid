/**
 * @file StlExportHandler.h
 * @brief Stl 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "StlModelHandler.h"

namespace systems::feature {
class StlExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "stl" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "STL stereolithography triangle mesh file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::StlModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
