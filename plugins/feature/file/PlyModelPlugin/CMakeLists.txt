add_library(PlyModelCodec OBJECT
    PlyModelHandler.cpp
)
set_target_properties(PlyModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(PlyModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PlyModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(PlyImportPlugin
    SOURCES $<TARGET_OBJECTS:PlyModelCodec>
    PLUGIN_H PlyImportPlugin.h
)
target_include_directories(PlyImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(PlyExportPlugin
    SOURCES $<TARGET_OBJECTS:PlyModelCodec>
    PLUGIN_H PlyExportPlugin.h
)
target_include_directories(PlyExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(Plytest)
