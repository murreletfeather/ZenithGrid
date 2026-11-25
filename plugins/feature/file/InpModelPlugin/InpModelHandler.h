#ifndef INP_MODEL_HANDLER_H
#define INP_MODEL_HANDLER_H
#include "Core.h"
#include "ModelPayload.h"
#include <filesystem>
#include <optional>
#include <vector>

class ModelLayer;

#include "AbaqusPrecessConverter.h"
#include "abaqus_io.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace systems::io {
namespace fs = std::filesystem;
class InpModelHandler {
public:
    InpModelHandler() = default;

    std::optional<ModelPayload> readModel(const fs::path& path);
    void writeComponents(const ModelLayer& mgr,
        const std::vector<Index>& component_ids,
        const fs::path& path);
};

}
#endif // !INP_MODEL_HANDLER_H
