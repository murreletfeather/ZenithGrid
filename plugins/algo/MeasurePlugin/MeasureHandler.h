/**
 * @file MeasureHandler.h
 * @brief 测量处理器声明：距离、角度、半径、长度、面积、体积、包围盒与重心
 * @author 范成通 email 1941804585@qq.com
 */
#pragma once
#include "AlgorithmHandler.h"
#include <any>
#include <vector>

namespace systems::algo {
class MeasureHandler : public AlgorithmHandler {
public:
    MeasureHandler() = default;
    ~MeasureHandler() override = default;

    std::any execute(HandlerContext& context, const std::vector<core::ArgObject>& args) override;
    std::vector<core::ArgType> args_type() const override;
};
}
