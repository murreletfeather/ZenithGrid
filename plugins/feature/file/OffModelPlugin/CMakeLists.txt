add_library(OffModelCodec OBJECT
    OffModelHandler.cpp
    OffMeshIO.cpp
)
set_target_properties(OffModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(OffModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(OffModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(OffImportPlugin
    SOURCES $<TARGET_OBJECTS:OffModelCodec>
    PLUGIN_H OffImportPlugin.h
)
target_include_directories(OffImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(OffExportPlugin
    SOURCES $<TARGET_OBJECTS:OffModelCodec>
    PLUGIN_H OffExportPlugin.h
)
target_include_directories(OffExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(test)
