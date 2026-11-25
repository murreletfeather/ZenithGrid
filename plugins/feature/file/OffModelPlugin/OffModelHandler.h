/**
 * @file OffModelHandler.h
 * @brief OFF 模型文件处理器
 */
#ifndef OFF_MODEL_HANDLER_H
#define OFF_MODEL_HANDLER_H
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
 * @brief OFF(Geomview Object File Format)文件格式处理器，读取和写出 OFF 文件
 */
class OffModelHandler {
public:
    OffModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !OFF_MODEL_HANDLER_H
