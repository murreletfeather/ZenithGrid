/**
 * @file StlModelHandler.h
 * @brief STL 三角网格文件处理器
 */
#ifndef STL_MODEL_HANDLER_H
#define STL_MODEL_HANDLER_H
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
 * @brief STL 三角网格格式处理器，读取和写出 STL 文件
 */
class StlModelHandler {
public:
    StlModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !STL_MODEL_HANDLER_H
