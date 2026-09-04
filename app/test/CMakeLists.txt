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

# 测试用翻译资源：构建微小 qm（/i18n/app_en_US.qm）内嵌进测试 exe，
# 使 QLanguageManager 在测试中可走真实的"装载→卸载"路径（覆盖 zh→en→zh 回归）
qt_add_translations(TestLanguageManager
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/app_en_US.ts
    RESOURCE_PREFIX /i18n
    SOURCE_TARGETS TestLanguageManager
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
