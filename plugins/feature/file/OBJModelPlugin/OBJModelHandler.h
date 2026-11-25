/**
 * @file OBJModelHandler.h
 * @author 张家僮(htxz_6a6@163.com)
 */
#ifndef OBJ_MODEL_HANDLER_H
#define OBJ_MODEL_HANDLER_H
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
 * @brief OBJ模型文件处理器
 */
class OBJModelHandler {
public:
    OBJModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path, bool split_groups = false);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !OBJ_MODEL_HANDLER_H 