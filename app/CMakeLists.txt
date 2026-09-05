find_package(KDDockWidgets-qt6 REQUIRED)

add_subdirectory(core)
add_subdirectory(model)
add_subdirectory(render)

# 1) 生成 App Bundle（macOS 上的 .app 目录结构）
set(app_icon_resource_windows "${CMAKE_SOURCE_DIR}/resource/PreCess.rc")
qt_add_executable(${MYNAME} MACOSX_BUNDLE main.cpp ${app_icon_resource_windows})
# 设置编译完成的组件在 qrc:/qt/qml
qt_add_qml_module(${MYNAME}
        URI app
        SOURCES
            TreeModel.h
            TreeModel.cpp
            QLogManager.h
            QLogManager.cpp
            QLanguageManager.h
            QLanguageManager.cpp
        QML_FILES
            Main.qml
            AppToolbar.qml
            GeometryOperationActions.qml
            AttributeRenderPanel.qml
            CentralRenderArea.qml
            SideBar.qml
            ObjectTree.qml
            Selector.qml
            JavaScriptConsole.qml
            PreferencesWindow.qml
            OutputLog.qml
)

# ===== 国际化（拆分协作式）=====
# 中文为源语言，仅打包英文翻译；每插件一份独立 .ts（归属各插件目录，团队成员各自维护，
# 互不冲突），主 ts 只含主程序 QML/适配器上下文。各 ts 独立生成 <基名>_en_US.qm；
# 注意：qt_add_translations 多次调用 + RESOURCE_PREFIX 会共用内部 qrc 名互相覆盖，
# 故一律用 QM_FILES_OUTPUT_VARIABLE 收集产物，最后统一一次 qt_add_resources 嵌入主程序。
# 插件 ts 基名清单与 app/QLanguageManager.cpp 的 kPluginTranslationBases 一一对应。

set(_PRECESS_I18N_QM_FILES "")

# 1) 主程序翻译：QML 文件（经 ${MYNAME}）与系统适配器源码
qt_add_translations(${MYNAME}
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/app_en_US.ts
    LUPDATE_OPTIONS -locations relative
    LUPDATE_TARGET PreCess_lupdate
    LRELEASE_TARGET PreCess_lrelease
    SOURCE_TARGETS
        # 必须包含 app 目标本体：显式传 SOURCE_TARGETS 时 lupdate 只扫描列表内目标，
        # 遗漏主目标会把 QML 源串标 vanished、被 lrelease 丢弃（qm 缺 QML 上下文）
        ${MYNAME}
        FeatureSystemQml EditSystemQml AlgorithmSystemQml IOSystemQml
    QM_FILES_OUTPUT_VARIABLE _PRECESS_MAIN_QM
)
list(APPEND _PRECESS_I18N_QM_FILES ${_PRECESS_MAIN_QM})

# 2) 插件翻译：每插件一份 ts（插件负责人各自维护），源集为该插件静态库
set(_PRECESS_I18N_PLUGINS
    feature/FeatureDemoPlugin/FeatureDemoPluginlib
    feature/MeasurePlugin/MeasurePluginlib
    feature/DimensionPlugin/DimensionPluginlib
    feature/MeshQualityPlugin/MeshQualityPluginlib
    feature/MeshRepairPlugin/MeshRepairPluginlib
    edit/CreateFacePlugin/CreateFacePluginlib
    edit/DeleteFacePlugin/DeleteFacePluginlib
    algo/TetGenPlugin/TetGenPluginlib
    algo/TetGenLibPlugin/TetGenLibPluginlib
    algo/GmshPlugin/GmshPluginlib
    algo/CmdExecutePlugin/CmdExecutePluginlib
)
foreach(_plugin_entry IN LISTS _PRECESS_I18N_PLUGINS)
    string(REGEX MATCH "^([^/]+)/([^/]+)/([^/]+)$" _m "${_plugin_entry}")
    set(_plugin_cat "${CMAKE_MATCH_1}")
    set(_plugin_name "${CMAKE_MATCH_2}")
    set(_plugin_lib "${CMAKE_MATCH_3}")
    set(_plugin_ts "${CMAKE_SOURCE_DIR}/plugins/${_plugin_cat}/${_plugin_name}/i18n/${_plugin_name}_en_US.ts")
    qt_add_translations(${MYNAME}
        TS_FILES ${_plugin_ts}
        LUPDATE_OPTIONS -locations relative
        LUPDATE_TARGET ${_plugin_name}_lupdate
        LRELEASE_TARGET ${_plugin_name}_lrelease
        SOURCE_TARGETS ${_plugin_lib}
        QM_FILES_OUTPUT_VARIABLE _plugin_qm
    )
    list(APPEND _PRECESS_I18N_QM_FILES ${_plugin_qm})
endforeach()

# 3) 统一嵌入：全部 qm 以 /i18n 前缀进入主程序资源（rcc 在 qm 生成之后运行）
set(_PRECESS_I18N_TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/app_en_US.ts)
add_custom_target(PreCess_i18n_qms DEPENDS ${_PRECESS_I18N_QM_FILES})
add_dependencies(${MYNAME} PreCess_i18n_qms)
foreach(_qm_file IN LISTS _PRECESS_I18N_QM_FILES)
    get_filename_component(_qm_name "${_qm_file}" NAME)
    set_source_files_properties(${_qm_file} PROPERTIES
        GENERATED TRUE
        QT_RESOURCE_ALIAS "${_qm_name}"
    )
endforeach()
qt_add_resources(${MYNAME} PreCess_translations PREFIX "/i18n" FILES ${_PRECESS_I18N_QM_FILES})

# 翻译检查：构建时校验全部 ts 无 vanished 条目（lupdate 扫描范围缩水的回归防护）
foreach(_plugin_entry IN LISTS _PRECESS_I18N_PLUGINS)
    string(REGEX MATCH "^([^/]+)/([^/]+)/([^/]+)$" _m "${_plugin_entry}")
    list(APPEND _PRECESS_I18N_TS_FILES
        "${CMAKE_SOURCE_DIR}/plugins/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}/i18n/${CMAKE_MATCH_2}_en_US.ts")
endforeach()
add_custom_target(check_translations
    COMMAND ${CMAKE_COMMAND} "-DTS_FILES=${_PRECESS_I18N_TS_FILES}"
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckTranslations.cmake
    COMMENT "检查全部翻译源文件（app + 各插件）中是否存在 vanished 条目"
    VERBATIM
)
add_dependencies(${MYNAME} check_translations)

add_subdirectory(test)
target_link_libraries(${MYNAME}
    PRIVATE
    renderQml renderQmlplugin modelQml modelQmlplugin
    VTK::GUISupportQtQuick
    Qt${VTK_QT_VERSION}::Quick
    KDAB::kddockwidgets
    spdlog::spdlog
)

# - Prevent the creation of a console window on Windows.
# - Create an application bundle on macOS.
set_target_properties(${MYNAME} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

target_compile_definitions(${MYNAME} PRIVATE
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>"
)

# 2) 安装 .app 到指定前缀下
include(InstallRequiredSystemLibraries)
install(TARGETS ${MYNAME}
    BUNDLE DESTINATION .          # 放在 install/ 目录下
    FRAMEWORK DESTINATION Frameworks
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
)

# 3) 安装依赖库 (RUNTIME_DEPENDENCIES)
#    - 这里要列举实际用到的模块动态库目录，否则无法复制对应的运行时依赖
set(DEPENDENCY_DIRECTORIES 
    $<TARGET_FILE_DIR:VTK::CommonCore>
    $<TARGET_FILE_DIR:TKernel>
    $<TARGET_FILE_DIR:Qt${VTK_QT_VERSION}::Core>
    $<TARGET_FILE_DIR:spdlog::spdlog>
    $<TARGET_FILE_DIR:KDAB::kddockwidgets>
)
if(freetype_FOUND)
    list(APPEND DEPENDENCY_DIRECTORIES $<TARGET_FILE_DIR:freetype>)
endif()
install(
    TARGETS ${MYNAME}
    RUNTIME_DEPENDENCIES
    DIRECTORIES ${DEPENDENCY_DIRECTORIES}
    PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"  # don't install Windows-provided libs
    POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"  # don't install Windows-provided libs
    FRAMEWORK DESTINATION Frameworks
    BUNDLE DESTINATION .
    COMPONENT ${MYNAME}
)

# 4) 生成部署 QML 的脚本（若需要更多 QML 插件，可在这里处理）
qt_generate_deploy_qml_app_script(
    TARGET ${MYNAME}
    OUTPUT_SCRIPT deploy_script
    NO_COMPILER_RUNTIME # 不在此包含运行时库，特别是vc_redist.exe。已通过InstallRequiredSystemLibraries声明安装运行时dll
#    DEPLOY_TOOL_OPTIONS --no-opengl-sw --no-system-dxc-compiler # 可选通过禁用一些功能来减小体积
)
install(
    SCRIPT ${deploy_script}
    COMPONENT ${MYNAME}
)

# 5) 使用 fixup_bundle 将所有依赖“收拢”进 .app，并修正引用路径（仅在 macOS 下执行）
if(APPLE)
  set(APP_BUNDLE_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/${MYNAME}.app/Contents/MacOS/${MYNAME}")

  install(CODE "
    include(BundleUtilities)
    # 使用 $ENV{DESTDIR} 拼接最终路径
    set(BUNDLE_PATH \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MYNAME}.app/Contents/MacOS/${MYNAME}\")
    message(STATUS \"fixup_bundle: processing \${BUNDLE_PATH}\")
    fixup_bundle(
      \"\${BUNDLE_PATH}\"
      \"\"
      \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Frameworks;/opt/homebrew/lib;/usr/local/lib\"
    )
  ")
endif()
