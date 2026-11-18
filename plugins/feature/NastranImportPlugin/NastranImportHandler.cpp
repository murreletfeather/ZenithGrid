#include "NastranImportHandler.h"
#include "FeatureContext.h"
#include "FeatureParams.h"
#include "FeatureRegistrar.h"
#include "MeshData.h"
#include "NastranReader.h"

namespace systems::feature {
namespace {
    ComponentDatas readNastranComponents(const std::filesystem::path& path, const job::ProgressFn& report)
    {
        auto mesh = readNastranMesh(path, report);
        auto component = std::make_unique<ComponentData>();
        component->name = "网格";
        component->mesh = std::move(mesh);

        ComponentDatas components;
        components.push_back(std::move(component));
        return components;
    }
}

void NastranImportHandler::setup(FeatureRegistrar& registrar, FeatureContext&)
{
    registrar.setFileType({ FileDirection::Import, { "nas", "bdf" } });
    registrar.addParameter({ ArgTypeEnum::Paths, "输入文件", "", "请选择 Nastran 网格文件" });
}

std::any NastranImportHandler::execute(FeatureContext& context)
{
    const auto* paths = context.params.value(0).get<ArgTypeEnum::Paths>();
    if (!paths)
        return std::string("请选择 Nastran 网格文件。");
    return context.importModels("导入 Nastran 网格", *paths, readNastranComponents);
}
}
