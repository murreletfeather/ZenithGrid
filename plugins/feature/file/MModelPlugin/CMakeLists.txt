add_library(MModelCodec OBJECT
    MModelHandler.cpp
)
set_target_properties(MModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(MModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(MModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(MImportPlugin
    SOURCES $<TARGET_OBJECTS:MModelCodec>
    PLUGIN_H MImportPlugin.h
)
target_include_directories(MImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(MExportPlugin
    SOURCES $<TARGET_OBJECTS:MModelCodec>
    PLUGIN_H MExportPlugin.h
)
target_include_directories(MExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(test)
