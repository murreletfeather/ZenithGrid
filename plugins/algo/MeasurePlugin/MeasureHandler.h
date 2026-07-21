/**
 * @file MeasureHandler.h
 * @brief 测量处理器声明：距离、角度、半径、长度、面积、体积、包围盒与重心
 * @author 范成通 email 1941804585@qq.com
 */
#pragma once
#include "AlgorithmHandler.h"
#include "InteractiveHandler.h"
#include <any>
#include <optional>
#include <vector>

namespace systems::algo {

/**
 * @brief 测量处理器：同时实现尺寸标注（AlgorithmHandler）与交互测量（InteractiveHandler）
 *
 * 尺寸标注：选择对象 + 执行 → 返回结果文本（GUI 线程调用）。
 * 交互测量：两点成线、共端点两线自动标注夹角（渲染线程经 InteractionService 驱动）。
 */
class MeasureHandler : public AlgorithmHandler, public InteractiveHandler {
public:
    MeasureHandler() = default;
    ~MeasureHandler() override = default;

    std::any execute(HandlerContext& context, const std::vector<core::ArgObject>& args) override;
    std::vector<core::ArgType> args_type() const override;

    //! @brief 交互测量状态查询（测试与面板用）
    int lineCount() const { return static_cast<int>(lines_.size()); }
    bool hasPending() const { return pending_.has_value(); }

    // ---- InteractiveHandler：交互测量 ----
    void onActivate() override { clear(); }
    void onDeactivate() override { clear(); }
    bool onPick(const PickInfo& pick) override;
    bool onHover(const PickInfo& pick) override;
    const AnnotationBatch& annotations() const override { return annotations_; }
    std::string resultText() const override;
    void clear() override;

private:
    //! @brief 交互测量线：两个吸附点（PickInfo 已含世界坐标与两套顶点 id）
    struct MeasureLine {
        PickInfo a, b;
    };
    //! @brief 共端点两线的夹角：at 为共点，p/q 为两线各自另一端点
    struct MeasureAngle {
        int line1 = 0; //> lines_ 下标
        int line2 = 0;
        std::array<double, 3> at;
        std::array<double, 3> p;
        std::array<double, 3> q;
        double angle = 0.0;
    };

    static bool samePoint(const PickInfo& a, const PickInfo& b);
    void addLine(const PickInfo& a, const PickInfo& b);
    //! @brief 由当前状态重建标注集
    void refreshAnnotations();

    std::optional<PickInfo> pending_; //> 已起笔未成线的首点
    bool has_preview_ = false; //> 悬停是否正在预览
    PickInfo preview_ {}; //> 悬停预览吸附点
    std::vector<MeasureLine> lines_;
    std::vector<MeasureAngle> angles_;
    AnnotationBatch annotations_;
};
}
