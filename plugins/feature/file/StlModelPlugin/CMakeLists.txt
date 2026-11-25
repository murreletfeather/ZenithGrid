add_library(StlModelCodec OBJECT
    StlModelHandler.cpp
)
set_target_properties(StlModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(StlModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(StlModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(StlImportPlugin
    SOURCES $<TARGET_OBJECTS:StlModelCodec>
    PLUGIN_H StlImportPlugin.h
)
target_include_directories(StlImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(StlExportPlugin
    SOURCES $<TARGET_OBJECTS:StlModelCodec>
    PLUGIN_H StlExportPlugin.h
)
target_include_directories(StlExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(test)
