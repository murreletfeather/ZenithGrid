set(CMAKE_AUTOMOC ON)

precess_add_test(TestLanguageManager
    TestLanguageManager.cpp
    ../QLanguageManager.cpp
)
precess_test_link_libraries(TestLanguageManager
    Qt${VTK_QT_VERSION}::Qml Core spdlog::spdlog
)
target_include_directories(TestLanguageManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../model
)

# 测试用翻译资源：微小 qm（/i18n/app_en_US.qm）经手工 lrelease 构建后内嵌进测试 exe，
# 使 QLanguageManager 在测试中可走真实的"装载→卸载"路径（覆盖 zh→en→zh 回归）。
# 注意：不走 qt_add_translations（避免被全局 update_translations 以无源码来源为由清除词条）。
# 守卫：BUILD_TESTING=OFF 时 precess_add_test 不建目标，引用会配置失败；无 LinguistTools
# 的平台（如 wasm）无 lrelease，整段跳过
if(BUILD_TESTING AND Qt${VTK_QT_VERSION}LinguistTools_FOUND)
    set(_test_i18n_ts ${CMAKE_CURRENT_SOURCE_DIR}/app_en_US.ts)
    set(_test_i18n_qm ${CMAKE_CURRENT_BINARY_DIR}/app_en_US.qm)
    add_custom_command(
        OUTPUT ${_test_i18n_qm}
        COMMAND Qt6::lrelease ${_test_i18n_ts} -qm ${_test_i18n_qm}
        DEPENDS ${_test_i18n_ts}
        COMMENT "生成测试用翻译 qm"
        VERBATIM
    )
    add_custom_target(TestLanguageManager_i18n_qm DEPENDS ${_test_i18n_qm})
    add_dependencies(TestLanguageManager TestLanguageManager_i18n_qm)
    set_source_files_properties(${_test_i18n_qm} PROPERTIES
        GENERATED TRUE
        QT_RESOURCE_ALIAS "app_en_US.qm"
    )
    qt_add_resources(TestLanguageManager test_i18n PREFIX "/i18n" FILES ${_test_i18n_qm})
endif()

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
    Qt${VTK_QT_VERSION}::Core Qt${VTK_QT_VERSION}::Qml Core modelQml
)
target_include_directories(TestDisplayTranslate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../model)
