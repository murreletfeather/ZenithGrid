#pragma once
#include "ModelIOHandler.h"

namespace systems::io {
//! @brief 将 Nastran Bulk Data 读取为模型载荷。
class NastranImportHandler : public ModelIOHandler {
public:
    std::optional<ModelPayload> read_model(const fs::path& path, const std::vector<std::any>& args) override;
    void write_components(const ModelLayer& model, const std::vector<Index>& component_ids,
        const fs::path& path, const std::vector<std::any>& args) override;
    std::vector<core::ArgType> read_args_type() const override;
    std::vector<core::ArgType> write_args_type() const override;
};
}
