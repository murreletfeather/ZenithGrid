/**
 * @file VtkXmlModelHandler.h
 * @brief VTK XML 网格文件处理器
 */
#ifndef VTK_XML_MODEL_HANDLER_H
#define VTK_XML_MODEL_HANDLER_H
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
 * @brief VTK XML 网格格式处理器，读取和写出 vtp / vtu 文件
 */
class VtkXmlModelHandler {
public:
    VtkXmlModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !VTK_XML_MODEL_HANDLER_H
