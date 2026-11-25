/**
 * @file OBJModelHandler.cpp
 * @author 张家僮(htxz_6a6@163.com)
 */
#include "OBJModelHandler.h"
#include "MeshData.h"
#include "ModelData.h"
#include "ObjMeshIO.h"
#include "ModelLayer.h"

#include <spdlog/spdlog.h>
#include <fstream>

namespace systems::io {
std::optional<ModelPayload> OBJModelHandler::readModel(const fs::path& path, bool split_groups)
{
    auto comps = ObjMeshIO::loadFromFile(path, split_groups);
    if (!comps) {
        return std::nullopt;
    }

    return ModelPayload{ path.filename().u8string(), std::move(*comps) };
}

void OBJModelHandler::writeComponents(const ModelLayer& mgr,
    const std::vector<Index>& component_ids,
    const fs::path& path)
{
    std::ofstream ofs(path);
    if (!ofs) {
        spdlog::error("OBJModelHandler: failed to open output file: {}", path.string());
        return;
    }

    // 按 id 从 ModelLayer 解析组件，写出逻辑复用 ObjMeshIO::saveToFile
    std::vector<const ComponentData*> components;
    for (Index cid : component_ids) {
        const ComponentData* comp = mgr.findComponent(cid);
        if (!comp) {
            spdlog::warn("OBJModelHandler: component {} not found, skip", cid);
            continue;
        }
        if (!comp->mesh) {
            spdlog::warn("OBJModelHandler: component {} has no mesh, skip", cid);
            continue;
        }
        if (comp->mesh->vertex_count_ <= 0) {
            spdlog::warn("OBJModelHandler: component {} has no vertices, skip", cid);
            continue;
        }
        components.push_back(comp);
    }

    ObjMeshIO::saveToFile(components, ofs);
}

}
