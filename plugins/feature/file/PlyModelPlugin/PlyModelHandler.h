/**
 * @file PlyModelHandler.h
 * @author 龚正(1740124400@qq.com)
 * 支持ASCII和二进制格式的PLY文件
 */
#ifndef PLY_MODEL_HANDLER_H
#define PLY_MODEL_HANDLER_H
#include "Core.h"
#include "ModelPayload.h"
#include <filesystem>
#include <optional>
#include <vector>

class ModelLayer;
#include <tinyply.h>

namespace systems::io {
namespace fs = std::filesystem;
/**
 * @brief PLY模型文件处理器
 */
class PlyModelHandler {
public:
    PlyModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !PLY_MODEL_HANDLER_H 
