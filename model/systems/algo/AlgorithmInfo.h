#ifndef ALGORITHM_INFO_H
#define ALGORITHM_INFO_H
#include "ArgType.h"
#include <string>
#include <vector>
namespace systems::algo {
struct AlgorithmInfo {
    std::string name; //> 算法唯一名称，用作索引
    std::string display_name; //> 算法UI展示用名称
    std::string description; //> 算法描述
    std::string result_display; //> 结果展示方式："popup" 弹窗展示，空或其他值默认侧栏展示
    bool interactive = false; //> 是否声明视口交互能力
    std::vector<core::ArgType> arg_types; //> 算法参数类型列表
};
}

#endif // ALGORITHM_INFO_H