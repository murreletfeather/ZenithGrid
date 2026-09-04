#include <QCoreApplication>
#include "CmdExecuteHandler.h"
#include "ArgObject.h"
#include "ModelIOSystemBase.h"
#include "ComponentOperator.h"
#include "ComponentData.h"
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {
// json 元数据中的 UI 字符串不会被 lupdate 扫描，此处以 NOOP 形式集中声明供提取；
// 注册串一律存源语言，运行时翻译由显示层按当前语言完成
void registerDisplayNameTranslationSources()
{
    QT_TRANSLATE_NOOP("PreCess", "执行cmd命令");
}
}

using core::ArgType;
std::any systems::algo::CmdExecuteHandler::execute(HandlerContext& context, const std::vector<core::ArgObject>& args)
{
    using namespace std;
    using namespace std::filesystem;
    if (args.size() < 2)
        return {};
    const path* cmd = args[0].get<ArgTypeEnum::Path>();
    const string* args_str = args[1].get<ArgTypeEnum::Text>();
    if (!cmd || !args_str) {
        spdlog::critical("CmdExecuteHandler: Invalid command or arguments.");
        return {};
    }

    path exe_dir = *cmd;
    exe_dir.remove_filename();

    // ====== component 粒度判断输入类型 ======
    const ComponentData& comp = context.cur_component.component();

    bool hasMesh = (comp.mesh != nullptr);
    bool hasGeometry = (comp.geometry != nullptr);

    if (!hasMesh && !hasGeometry) {
        spdlog::error("CmdExecuteHandler: component {} has neither mesh nor geometry",
            context.cur_component.componentId());
        return {};
    }

    // ====== 决定输入文件名与格式 ======
    string input_file;
    string file_type;
    if (hasMesh) {
        input_file = "input.obj";
        file_type = "Wavefront .obj file";
    } else {
        input_file = "input.stp";
        file_type = "ISO 10303-21";
    }

    // 直接导出当前 component
    context.io_system.writeComponents({ context.cur_component.componentId() },
        exe_dir / input_file,
        file_type,
        {});

    // 执行外部命令
    std::string full_command { cmd->string() + " " + input_file + " " + *args_str };
    spdlog::debug("CmdExecuteHandler: Executing command: {}", full_command);
    std::system(full_command.c_str());

    // 读输出（read 是导入新模型，不依赖 component）
    path output_path;
    if (exists(output_path = exe_dir / "output.obj")) {
        context.io_system.read(output_path, "Wavefront .obj file", {});
    } else if (exists(output_path = exe_dir / "output.stp")) {
        context.io_system.read(output_path, "ISO 10303-21", {});
    } else {
        spdlog::critical("CmdExecuteHandler: output file not found in {}", exe_dir.string());
    }
    return {};
}

std::vector<ArgType> systems::algo::CmdExecuteHandler::args_type() const
{
    // 复用CmdExecuteCommand参数模型
    return {
        ArgType { ArgTypeEnum::Path, QT_TRANSLATE_NOOP("PreCess", "功能(.exe,.bat)"), "" },
        ArgType { ArgTypeEnum::Text, QT_TRANSLATE_NOOP("PreCess", "参数列表"), "" }
    };
}
