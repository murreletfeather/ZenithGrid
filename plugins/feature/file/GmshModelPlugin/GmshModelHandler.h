/**
 * @file GmshModelHandler.h
 * @brief Gmsh 网格文件处理器
 */
#ifndef GMSH_MODEL_HANDLER_H
#define GMSH_MODEL_HANDLER_H
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
 * @brief Gmsh 网格格式处理器，读取和写出 msh 文件
 */
class GmshModelHandler {
public:
    GmshModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !GMSH_MODEL_HANDLER_H
