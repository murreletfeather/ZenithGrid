add_library(OBJModelCodec OBJECT
    OBJModelHandler.cpp
)
set_target_properties(OBJModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(OBJModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(OBJModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(OBJImportPlugin
    SOURCES $<TARGET_OBJECTS:OBJModelCodec>
    PLUGIN_H OBJImportPlugin.h
)
target_include_directories(OBJImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(OBJExportPlugin
    SOURCES $<TARGET_OBJECTS:OBJModelCodec>
    PLUGIN_H OBJExportPlugin.h
)
target_include_directories(OBJExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(test)
