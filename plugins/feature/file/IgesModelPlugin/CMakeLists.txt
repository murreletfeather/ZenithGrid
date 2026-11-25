set(codec_libraries TKDEIGES TKXCAF TKVCAF TKCAF TKLCAF)

add_library(IgesModelCodec OBJECT
    IgesModelHandler.cpp
    IgesXdeComponentBuilder.cpp
)
set_target_properties(IgesModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(IgesModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(IgesModelCodec PRIVATE PreCess::Base ${codec_libraries})

precess_add_feature_plugin(IgesImportPlugin
    SOURCES $<TARGET_OBJECTS:IgesModelCodec>
    PLUGIN_H IgesImportPlugin.h
)
target_include_directories(IgesImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(IgesImportPlugin ${codec_libraries})

precess_add_feature_plugin(IgesExportPlugin
    SOURCES $<TARGET_OBJECTS:IgesModelCodec>
    PLUGIN_H IgesExportPlugin.h
)
target_include_directories(IgesExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
precess_plugin_link_libraries(IgesExportPlugin ${codec_libraries})

add_subdirectory(test)
