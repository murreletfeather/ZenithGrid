set(codec_libraries VTK::IOLegacy VTK::FiltersCore)

add_library(VtkLegacyModelCodec OBJECT
    VtkLegacyModelHandler.cpp
)
set_target_properties(VtkLegacyModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(VtkLegacyModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(VtkLegacyModelCodec PRIVATE PreCess::Base ${codec_libraries})

precess_add_feature_plugin(VtkLegacyImportPlugin
    SOURCES $<TARGET_OBJECTS:VtkLegacyModelCodec>
    PLUGIN_H VtkLegacyImportPlugin.h
)
target_include_directories(VtkLegacyImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(VtkLegacyImportPlugin ${codec_libraries})

precess_add_feature_plugin(VtkLegacyExportPlugin
    SOURCES $<TARGET_OBJECTS:VtkLegacyModelCodec>
    PLUGIN_H VtkLegacyExportPlugin.h
)
target_include_directories(VtkLegacyExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(VtkLegacyExportPlugin ${codec_libraries})

add_subdirectory(test)
