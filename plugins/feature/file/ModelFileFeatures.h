/**
 * @file ModelFileFeatures.h
 * @brief 导入和导出 Feature 共用的参数声明与文件任务。
 */
#pragma once
#include "ArgType.h"
#include "FeatureContext.h"
#include "FeatureParams.h"
#include "FeatureRegistrar.h"
#include "ModelLayer.h"
#include "ModelPayload.h"
#include "Selection.h"

#include <any>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace systems::feature {
namespace file {
    inline void setupImport(FeatureRegistrar& registrar, std::vector<std::string> extensions)
    {
        registrar.setFileType({ FileDirection::Import, std::move(extensions) });
        registrar.addParameter({ ArgTypeEnum::Paths, "输入文件", "", "请选择文件路径" });
    }

    template <typename ReadModel>
    std::any importModels(FeatureContext& context, const std::string& label, ReadModel read)
    {
        const auto* paths = context.params.value(0).get<ArgTypeEnum::Paths>();
        if (!paths || paths->empty())
            return std::string("请选择文件路径。");
        return context.runFileImport(label, [paths = *paths, read = std::move(read)](job::ProgressFn report) mutable {
            std::vector<ModelPayload> models;
            models.reserve(paths.size());
            for (std::size_t index = 0; index < paths.size(); ++index) {
                const auto& path = paths[index];
                const auto path_text = pathUtf8(path);
                report(static_cast<double>(index) / paths.size(), path_text);
                auto payload = read(path);
                if (payload)
                    models.push_back(std::move(*payload));
                else
                    spdlog::warn("文件解析失败，已跳过 {}", path_text);
            }
            if (models.empty())
                throw std::runtime_error("所选文件均未能解析。");
            const auto skipped = paths.size() - models.size();
            report(1.0, skipped == 0 ? "文件解析结束" : "部分文件解析失败，已跳过 " + std::to_string(skipped) + " 个文件");
            return models;
        });
    }

    inline void setupExport(FeatureRegistrar& registrar, std::vector<std::string> extensions)
    {
        registrar.setFileType({ FileDirection::Export, std::move(extensions) });
        registrar.addParameter({ ArgTypeEnum::Path, "输出文件", "", "请选择文件路径" });
        registrar.addParameter({ ArgTypeEnum::Selector, "导出组件", "Component", "请选择要导出的一个或多个组件" });
    }

    template <typename WriteComponents>
    std::any exportComponents(FeatureContext& context, const std::string& label, WriteComponents write)
    {
        const auto* path = context.params.value(0).get<ArgTypeEnum::Path>();
        if (!path || path->empty())
            return std::string("请选择文件路径。");
        const auto* selection = context.params.value(1).get<ArgTypeEnum::Selector>();
        const auto* selected = selection ? selection->get() : nullptr;
        if (!selected || selected->type != ElementEnum::Component || selected->ids.empty())
            return std::string("请选择要导出的组件。");
        for (const auto id : selected->ids) {
            if (!context.model.findComponent(id))
                return std::string("导出组件已失效，请重新选择。");
        }
        return context.runFileExport(label, [path = *path, ids = selected->ids, write = std::move(write)](const ModelLayer& layer, job::ProgressFn report) mutable {
            report(0.0, "正在导出文件");
            write(layer, ids, path);
            report(1.0, "导出处理结束");
        });
    }
}
}
