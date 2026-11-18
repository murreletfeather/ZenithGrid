/**
 * @file FeatureContext.cpp
 * @brief 固定功能服务：查询宿主，任务与预览按构造身份交给框架。
 */
#include "FeatureContext.h"
#include "FeatureSystem.h"
#include "ModelLayer.h"
#include "UndoStack.h"

#include <stdexcept>

namespace systems::feature {
namespace {
    struct ImportedModel {
        std::string name;
        ComponentDatas components;
    };

    std::vector<ImportedModel> readImportBatch(const std::vector<std::filesystem::path>& paths,
        const ImportComponentsFn& read, const systems::job::ProgressFn& report)
    {
        std::vector<ImportedModel> imports;
        imports.reserve(paths.size());
        for (std::size_t index = 0; index < paths.size(); ++index) {
            const auto file_name = paths[index].filename().u8string();
            const std::string name(reinterpret_cast<const char*>(file_name.data()), file_name.size());
            const auto report_file = [&](double progress, const std::string& stage) {
                report((static_cast<double>(index) + progress) / paths.size(), stage.empty() ? name : stage);
            };
            report_file(0.0, name);
            auto components = read(paths[index], report_file);
            if (components.empty())
                throw std::runtime_error("未从文件中读取到组件：" + name);
            imports.push_back({ name, std::move(components) });
        }
        return imports;
    }

    void installImportedModels(ModelLayer& model, std::vector<ImportedModel>& imports, systems::job::ProgressFn report)
    {
        for (auto& imported : imports)
            model.addModel(imported.name, std::move(imported.components));
        report(1.0, "导入完成");
    }
}

UndoContext::UndoContext(FeatureSystem& system, std::string owner)
    : system_(system)
    , owner_(std::move(owner))
{
}

bool UndoContext::beginScope(std::string label)
{
    auto* stack = system_.undo_stack_;
    UndoStack::OwnerScope owner_scope(stack, owner_);
    return stack && stack->beginScope(std::move(label));
}

void UndoContext::cancelScope()
{
    auto* stack = system_.undo_stack_;
    UndoStack::OwnerScope owner_scope(stack, owner_);
    if (!stack || stack->scopeId() == 0)
        return;
    if (system_.model_layer_->writesPending() && !system_.model_layer_->hasWritePrivilege()) {
        system_.cancelPendingJobs(owner_);
        if (system_.deferCleanup([stack, owner = owner_, scope_id = stack->scopeId()] {
                UndoStack::OwnerScope owner_scope(stack, owner);
                if (stack->scopeId() == scope_id)
                    stack->cancelScope();
            }))
            return;
    }
    stack->cancelScope();
}

void UndoContext::revertScope()
{
    auto* stack = system_.undo_stack_;
    UndoStack::OwnerScope owner_scope(stack, owner_);
    if (stack && stack->scopeId() != 0)
        stack->revertScope();
}

bool UndoContext::scopeActive() const
{
    auto* stack = system_.undo_stack_;
    UndoStack::OwnerScope owner_scope(stack, owner_);
    return stack && stack->scopeId() != 0;
}

FeatureContext::FeatureContext(FeatureSystem& system, std::string owner, FeatureEventGateway& events,
    FeatureParams& params, InteractionContext& interaction)
    : model(*system.model_layer_)
    , events(events)
    , params(params)
    , interaction(interaction)
    , undo(system, owner)
    , system_(system)
    , owner_(std::move(owner))
{
}

std::optional<Index> FeatureContext::activeModel() const
{
    return system_.active_model_provider_ ? system_.active_model_provider_() : std::nullopt;
}

std::optional<Index> FeatureContext::activeComponent() const
{
    return system_.active_component_provider_ ? system_.active_component_provider_() : std::nullopt;
}

std::optional<ComponentOperator> FeatureContext::componentOperator(Index component_id) const
{
    return model.getComponentOperator(component_id);
}

std::any FeatureContext::importModels([[maybe_unused]] std::string label, std::vector<std::filesystem::path> paths, ImportComponentsFn read)
{
    if (paths.empty())
        return std::string("请选择文件路径。");
    if (const auto* stack = system_.undo_stack_; stack && (!stack->inOperation() || stack->inPreviewCallback()))
        throw std::logic_error("文件导入必须从正式执行入口调用。");

#ifndef __EMSCRIPTEN__
    if (system_.job_runner_) {
        return runWritebackJob(std::move(label), [paths = std::move(paths)](const ModelLayer&) { return paths; }, [read = std::move(read)](const std::vector<std::filesystem::path>& inputs, systems::job::ProgressFn report) { return readImportBatch(inputs, read, report); }, installImportedModels, true);
    }
#endif

    // 同步宿主复用整批校验与回写；后台未提交时由调用方按既有任务结果处理。
    model.assertOperationIdle();
    const systems::job::ProgressFn report = [](double, const std::string&) { };
    auto imports = readImportBatch(paths, read, report);
    installImportedModels(model, imports, report);
    return { };
}

std::shared_ptr<systems::job::Job> FeatureContext::runComputeJob(std::string label, systems::job::JobTaskFn task)
{
    UndoStack::OwnerScope owner_scope(system_.undo_stack_, owner_);
    return system_.submitComputeJob(std::move(label), std::move(task), owner_);
}

std::shared_ptr<systems::job::Job> FeatureContext::runComponentJob(std::string label, Index component_id, ComponentJobTaskFn task)
{
    UndoStack::OwnerScope owner_scope(system_.undo_stack_, owner_);
    return system_.submitComponentJob(std::move(label), component_id, std::move(task), owner_);
}

std::shared_ptr<systems::job::Job> FeatureContext::runCapturedWriteback(std::string label, Index component_id,
    CaptureJobFn capture, WritebackFn write, bool masked)
{
    UndoStack::OwnerScope owner_scope(system_.undo_stack_, owner_);
    return system_.submitCapturedWriteback(std::move(label), component_id, std::move(capture), std::move(write), owner_, masked);
}

std::shared_ptr<systems::job::Job> FeatureContext::runCapturedWriteback(std::string label,
    LayerCaptureJobFn capture, LayerWritebackFn write, bool masked)
{
    UndoStack::OwnerScope owner_scope(system_.undo_stack_, owner_);
    return system_.submitCapturedWriteback(std::move(label), std::move(capture), std::move(write), owner_, masked);
}
}
