#pragma once
#include "FeatureHandler.h"

namespace systems::feature {
//! @brief 注册只读的 Nastran 文件入口，按文件安装网格组件。
class NastranImportHandler : public FeatureHandler {
public:
    void setup(FeatureRegistrar& registrar, FeatureContext& context) override;
    std::any execute(FeatureContext& context) override;
};
}
