/**
 * @file VtkLegacyModelHandler.h
 * @author 张家僮(htxz_6a6@163.com)
 */
#ifndef VTK_LEGACY_MODEL_HANDLER_H
#define VTK_LEGACY_MODEL_HANDLER_H
#include "Core.h"
#include "ModelPayload.h"
#include <filesystem>
#include <optional>
#include <vector>

class ModelLayer;

class ModelData;

namespace systems::io {
namespace fs = std::filesystem;
/**
 * @brief VTK模型文件处理器
 */
class VtkLegacyModelHandler {
public:
    VtkLegacyModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !VTK_LEGACY_MODEL_HANDLER_H 