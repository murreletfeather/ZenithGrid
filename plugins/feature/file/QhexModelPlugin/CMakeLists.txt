add_library(QhexModelCodec OBJECT
    QhexModelHandler.cpp
)
set_target_properties(QhexModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(QhexModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(QhexModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(QhexImportPlugin
    SOURCES $<TARGET_OBJECTS:QhexModelCodec>
    PLUGIN_H QhexImportPlugin.h
)
target_include_directories(QhexImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(QhexExportPlugin
    SOURCES $<TARGET_OBJECTS:QhexModelCodec>
    PLUGIN_H QhexExportPlugin.h
)
target_include_directories(QhexExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(test)
