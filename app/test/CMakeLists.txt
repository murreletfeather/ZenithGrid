set(CMAKE_AUTOMOC ON)

precess_add_test(TestLanguageManager
    TestLanguageManager.cpp
    ../QLanguageManager.cpp
)
precess_test_link_libraries(TestLanguageManager
    Qt${VTK_QT_VERSION}::Qml Core
)
target_include_directories(TestLanguageManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../model
)

precess_add_test(TestFeatureAdaptorLanguageNotify
    TestFeatureAdaptorLanguageNotify.cpp
)
precess_test_link_libraries(TestFeatureAdaptorLanguageNotify
    FeatureSystemQml modelQml FeatureSystem Data
)

precess_add_test(TestDisplayTranslate
    TestDisplayTranslate.cpp
)
precess_test_link_libraries(TestDisplayTranslate
    Qt${VTK_QT_VERSION}::Core Core
)
target_include_directories(TestDisplayTranslate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../model)
