set(codec_libraries TKDESTEP TKXCAF TKVCAF TKCAF TKLCAF)

add_library(StepModelCodec OBJECT
    StepModelHandler.cpp
    StepXdeComponentBuilder.cpp
)
set_target_properties(StepModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(StepModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(StepModelCodec PRIVATE PreCess::Base ${codec_libraries})

precess_add_feature_plugin(StepImportPlugin
    SOURCES $<TARGET_OBJECTS:StepModelCodec>
    PLUGIN_H StepImportPlugin.h
)
target_include_directories(StepImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(StepImportPlugin ${codec_libraries})

precess_add_feature_plugin(StepExportPlugin
    SOURCES $<TARGET_OBJECTS:StepModelCodec>
    PLUGIN_H StepExportPlugin.h
)
target_include_directories(StepExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(StepExportPlugin ${codec_libraries})

add_subdirectory(test)
