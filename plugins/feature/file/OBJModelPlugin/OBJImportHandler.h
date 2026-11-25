/**
 * @file OBJImportHandler.h
 * @brief 导入 OBJ 并按用户指定的方式处理面分组。
 */
#pragma once
#include "FeatureHandler.h"
#include "ModelFileFeatures.h"
#include "OBJModelHandler.h"

namespace systems::feature {
class OBJImportHandler final : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext&) override
    {
        file::setupImport(registrar, { "obj" });
        registrar.addParameter({ ArgTypeEnum::Bool, "按 g 分组拆分组件", "false",
            "关闭时合并分组的面。组名前缀作为面属性名，末尾的数字作为值，缺值填 0。" });
    }

    std::any execute(FeatureContext& context) override
    {
        const auto* split_groups = context.params.value(1).get<ArgTypeEnum::Bool>();
        if (!split_groups)
            throw std::invalid_argument("OBJ 分组选项无效。");
        return file::importModels(context, "Wavefront .obj file", [split_groups = *split_groups](const std::filesystem::path& path) {
            return io::OBJModelHandler().readModel(path, split_groups);
        });
    }
};
}
