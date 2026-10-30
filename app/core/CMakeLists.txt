# 与 ArgType 一样注入完整声明，原生枚举变化时重新配置并生成 Qt 枚举。
set(ElementEnum_HEADER "${PROJECT_SOURCE_DIR}/core/private_ElementEnum.h")
file(READ "${ElementEnum_HEADER}" ElementEnum_H_CONTENT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ElementEnum_HEADER}")
configure_file(QElement.h.in QElement.h @ONLY)

set_source_files_properties(App.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(coreQml 
    URI app.core 
    STATIC 
    SOURCES QSelection.h AppPalette.h ThemePalette.h WallpaperLibrary.h WallpaperLibrary.cpp "${CMAKE_CURRENT_BINARY_DIR}/QElement.h"
    QML_FILES App.qml
)
target_include_directories(coreQml PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # 头文件路径
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(coreQml PUBLIC Core)
target_link_libraries(coreQml PRIVATE Qt${VTK_QT_VERSION}::Qml Qt${VTK_QT_VERSION}::Gui Qt${VTK_QT_VERSION}::Quick spdlog::spdlog)