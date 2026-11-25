add_library(InpModelCodec OBJECT
    InpModelHandler.cpp
    abaqus_io.cpp
    AbaqusPrecessConverter.cpp
)
set_target_properties(InpModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(InpModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(InpModelCodec PRIVATE PreCess::Base)

precess_add_feature_plugin(InpImportPlugin
    SOURCES $<TARGET_OBJECTS:InpModelCodec>
    PLUGIN_H InpImportPlugin.h
)
target_include_directories(InpImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

precess_add_feature_plugin(InpExportPlugin
    SOURCES $<TARGET_OBJECTS:InpModelCodec>
    PLUGIN_H InpExportPlugin.h
)
target_include_directories(InpExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_subdirectory(Inptest)
