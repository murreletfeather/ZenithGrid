/**
 * @file QhexModelHandler.h
 * @brief Qhex 六面体网格文件处理器
 */
#ifndef QHEX_MODEL_HANDLER_H
#define QHEX_MODEL_HANDLER_H
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
 * @brief Qhex 实验室六面体网格格式处理器，读取和写出 Qhex 文件
 */
class QhexModelHandler {
public:
    QhexModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !QHEX_MODEL_HANDLER_H
