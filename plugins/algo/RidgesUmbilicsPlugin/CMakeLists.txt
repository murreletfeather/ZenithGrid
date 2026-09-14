# RidgesUmbilicsPlugin CMake 配置
# CGAL Ridges_3 脊线与脐点近似算法插件；Monge jet 拟合（Jet_fitting_3）依赖 Eigen 头文件

# Eigen 为纯头文件库（MPL2），随 PreCess-deps 分发到 <deps>/eigen-3.4.0；
# 缺失时跳过本插件，不阻塞其余目标配置（libMeshb 先例）
find_path(EIGEN3_INCLUDE_DIR
    Eigen/src/Core/util/Macros.h
    PATH_SUFFIXES eigen-3.4.0 eigen3
    DOC "Eigen 头文件目录（PreCess-deps 下的 eigen-3.4.0）"
)
if(NOT EIGEN3_INCLUDE_DIR)
    message(WARNING "未找到 Eigen 头文件目录（<deps>/eigen-3.4.0），跳过 RidgesUmbilicsPlugin")
    return()
endif()

precess_add_algo_plugin(RidgesUmbilicsPlugin
    SOURCES "RidgesUmbilicsHandler.cpp"
    PLUGIN_H "RidgesUmbilicsPlugin.h"
)

# CGAL 与 Boost 头文件经 CgalSupport 公开传播，Eigen 由本插件单独注入
precess_plugin_link_libraries(RidgesUmbilicsPlugin CgalSupport)
target_include_directories(RidgesUmbilicsPluginlib PRIVATE ${EIGEN3_INCLUDE_DIR})

add_subdirectory(test)
