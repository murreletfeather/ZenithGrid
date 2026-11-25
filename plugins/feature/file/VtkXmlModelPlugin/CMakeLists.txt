set(codec_libraries VTK::IOXML)

add_library(VtkXmlModelCodec OBJECT
    VtkXmlModelHandler.cpp
)
set_target_properties(VtkXmlModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(VtkXmlModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(VtkXmlModelCodec PRIVATE PreCess::Base ${codec_libraries})

precess_add_feature_plugin(VtkXmlImportPlugin
    SOURCES $<TARGET_OBJECTS:VtkXmlModelCodec>
    PLUGIN_H VtkXmlImportPlugin.h
)
target_include_directories(VtkXmlImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(VtkXmlImportPlugin ${codec_libraries})

precess_add_feature_plugin(VtkXmlExportPlugin
    SOURCES $<TARGET_OBJECTS:VtkXmlModelCodec>
    PLUGIN_H VtkXmlExportPlugin.h
)
target_include_directories(VtkXmlExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(VtkXmlExportPlugin ${codec_libraries})

add_subdirectory(test)
