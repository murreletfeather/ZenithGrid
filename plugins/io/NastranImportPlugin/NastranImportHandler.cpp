#include "NastranImportHandler.h"
#include "ArgType.h"
#include "ComponentData.h"
#include "MeshData.h"
#include "NastranReader.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace systems::io {
std::optional<ModelPayload> NastranImportHandler::read_model(const fs::path& path, const std::vector<std::any>&)
{
    auto component = std::make_unique<ComponentData>();
    component->name = "网格";
    component->mesh = readNastranMesh(path, [](double, const std::string&) { });

    ModelPayload payload { path.filename().u8string(), {} };
    payload.components.push_back(std::move(component));
    return payload;
}

void NastranImportHandler::write_components(const ModelLayer&, const std::vector<Index>&,
    const fs::path&, const std::vector<std::any>&)
{
    throw std::logic_error("Nastran 格式仅支持导入");
}

std::vector<core::ArgType> NastranImportHandler::read_args_type() const
{
    return {};
}

std::vector<core::ArgType> NastranImportHandler::write_args_type() const
{
    return {};
}
}
