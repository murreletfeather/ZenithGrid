# RidgesUmbilicsPlugin CMake 配置
# CGAL Ridges_3 脊线与脐点近似分析功能插件；Monge jet 拟合（Jet_fitting_3）依赖 Eigen 头文件

# Eigen 为纯头文件库（MPL2）。两种依赖形态：PreCess-deps.py git 克隆安装布局
# （<deps>/eigen-3.4.0/include/eigen3）与旧压缩包解压布局（<deps>/eigen-3.4.0 根
# 下直接放 Eigen/，兼容既有依赖目录）；缺失时跳过本插件，不阻塞其余目标配置
# （libMeshb 先例）
find_path(EIGEN3_INCLUDE_DIR
    Eigen/src/Core/util/Macros.h
    PATH_SUFFIXES eigen-3.4.0/include/eigen3 eigen-3.4.0 eigen3
    DOC "Eigen 头文件目录（PreCess-deps 下的 eigen-3.4.0）"
)
if(NOT EIGEN3_INCLUDE_DIR)
    message(WARNING "未找到 Eigen 头文件目录（<deps>/eigen-3.4.0），跳过 RidgesUmbilicsPlugin")
    return()
endif()

precess_add_feature_plugin(RidgesUmbilicsPlugin
    SOURCES "RidgesUmbilicsHandler.cpp"
    PLUGIN_H "RidgesUmbilicsPlugin.h"
)

# CGAL 与 Boost 头文件经 CgalSupport 公开传播，Eigen 由本插件单独注入
precess_plugin_link_libraries(RidgesUmbilicsPlugin CgalSupport)
target_include_directories(RidgesUmbilicsPluginlib PRIVATE ${EIGEN3_INCLUDE_DIR})

# Monge 拟合逐顶点循环的并行加速：OpenMP 是编译器内置特性（无外部依赖），
# 可用时启用并行；不可用时 #pragma omp 被编译器忽略，自动退化为顺序执行。
# Emscripten 的 OpenMP 依赖 wasm 线程（实验性），直接跳过保持顺序执行
if(NOT EMSCRIPTEN)
    find_package(OpenMP COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(RidgesUmbilicsPluginlib PRIVATE OpenMP::OpenMP_CXX)
    endif()
endif()

add_subdirectory(test)
