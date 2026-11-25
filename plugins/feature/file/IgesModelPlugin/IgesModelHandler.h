/**
 * @file IgesModelHandler.h
 * @brief IGES 模型文件处理器
 * @author 范成通
 */
#ifndef IGES_MODEL_HANDLER_H
#define IGES_MODEL_HANDLER_H
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
 * @brief IGES 文件格式处理器，读取和写入 IGES 文件
 */
class IgesModelHandler {
public:
    IgesModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !IGES_MODEL_HANDLER_H