add_library(GmshModelCodec OBJECT
    GmshModelHandler.cpp
)
set_target_properties(GmshModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(GmshModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(GmshModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(GmshImportPlugin
    SOURCES $<TARGET_OBJECTS:GmshModelCodec>
    PLUGIN_H GmshImportPlugin.h
)
target_include_directories(GmshImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(GmshExportPlugin
    SOURCES $<TARGET_OBJECTS:GmshModelCodec>
    PLUGIN_H GmshExportPlugin.h
)
target_include_directories(GmshExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(test)
