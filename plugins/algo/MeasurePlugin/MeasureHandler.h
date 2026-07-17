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
