/**
 * @file InteractiveHandler.h
 * @brief 视口交互处理器接口：插件实现以提供"视口内交互"能力
 */
#ifndef INTERACTIVE_HANDLER_H
#define INTERACTIVE_HANDLER_H

#include "InteractiveTypes.h"

#include <string>

namespace systems::interaction {

/**
 * @brief 视口交互处理器接口
 *
 * 生命周期：onActivate 一次 → onPick/onHover 若干 → onDeactivate 一次。
 * 渲染层负责拾取解析与标注绘制，本接口只含数据；方法可能在渲染线程被调用，
 * 实现方需注意与 GUI 线程的互斥（通常由 UI 层的模式互斥保证）。
 */
class InteractiveHandler {
public:
    virtual ~InteractiveHandler() = default;

    //! @brief 激活交互（通常清空上一轮状态）
    virtual void onActivate() { }
    //! @brief 退出交互（渲染层随后清空标注）
    virtual void onDeactivate() { }

    //! @brief 左键点击：吸附点已解析完成
    //! @return 是否有状态变化（需要刷新标注与结果文本）
    virtual bool onPick(const PickInfo& pick) = 0;

    //! @brief 悬停：需要动态预览时更新标注集
    //! @return 是否更新了预览（需要刷新标注）
    virtual bool onHover(const PickInfo& pick) { return false; }

    //! @brief 当前应显示的标注集（事件后由渲染层拉取绘制）
    virtual const AnnotationBatch& annotations() const = 0;

    //! @brief 面板展示文本（无结果时为空串）
    virtual std::string resultText() const { return {}; }

    //! @brief 清空当前交互状态与标注
    virtual void clear() { }
};

} // namespace systems::interaction

#endif // INTERACTIVE_HANDLER_H
