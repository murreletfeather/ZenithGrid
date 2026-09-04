set(CMAKE_AUTOMOC ON)

precess_add_test(TestLanguageManager
    TestLanguageManager.cpp
    ../QLanguageManager.cpp
)
precess_test_link_libraries(TestLanguageManager
    Qt${VTK_QT_VERSION}::Qml
)
target_include_directories(TestLanguageManager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

precess_add_test(TestFeatureAdaptorLanguageNotify
    TestFeatureAdaptorLanguageNotify.cpp
)
precess_test_link_libraries(TestFeatureAdaptorLanguageNotify
    FeatureSystemQml modelQml FeatureSystem Data
)
