/**
 * @file QhexExportHandler.h
 * @brief Qhex 导出 Feature。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "QhexModelHandler.h"

namespace systems::feature {
class QhexExportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupExport(registrar, { "qhex" });
    }

    std::any execute(FeatureContext& context) override
    {
        return file::exportComponents(context, "Qhex hexahedral mesh file",
            [](const ModelLayer& layer, const std::vector<Index>& ids, const std::filesystem::path& path) {
                io::QhexModelHandler().writeComponents(layer, ids, path);
            });
    }
};
}
