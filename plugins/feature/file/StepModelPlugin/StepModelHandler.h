/**
 * @file StepModelHandler.h
 * @author 张家僮(htxz_6a6@163.com)
 */
#ifndef M_MODEL_HANDLER_H
#define M_MODEL_HANDLER_H
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
class StepModelHandler {
public:
    StepModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !M_MODEL_HANDLER_H 