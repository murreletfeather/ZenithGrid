find_package(libMeshb)
if(libMeshb_FOUND)
    set(codec_libraries Meshb.7)

    add_library(MeshMeditModelCodec OBJECT
        MeshMeditModelHandler.cpp
        LibMeshbIO.cpp
    )
    set_target_properties(MeshMeditModelCodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(MeshMeditModelCodec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(MeshMeditModelCodec PRIVATE PreCess::Base ${codec_libraries})

    precess_add_feature_plugin(MeshMeditImportPlugin
        SOURCES $<TARGET_OBJECTS:MeshMeditModelCodec>
        PLUGIN_H MeshMeditImportPlugin.h
    )
    target_include_directories(MeshMeditImportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    precess_plugin_link_libraries(MeshMeditImportPlugin ${codec_libraries})

    precess_add_feature_plugin(MeshMeditExportPlugin
        SOURCES $<TARGET_OBJECTS:MeshMeditModelCodec>
        PLUGIN_H MeshMeditExportPlugin.h
    )
    target_include_directories(MeshMeditExportPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    precess_plugin_link_libraries(MeshMeditExportPlugin ${codec_libraries})

    add_subdirectory(test)
endif()
