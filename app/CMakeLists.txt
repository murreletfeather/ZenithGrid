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

# 国际化：中文为源语言（界面字符串以中文编写），仅打包英文翻译 en_US.qm；
# lupdate 自动扫描本目标源码（含 QML 文件）维护 i18n/app_en_US.ts，
# 插件与适配器源码经 SOURCE_TARGETS 一并扫描（QT_TRANSLATE_NOOP 声明的动态注册字符串），
# lrelease 编译的 .qm 以 /i18n 前缀嵌入资源，QLanguageManager 运行时按需装载
qt_add_translations(${MYNAME}
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/app_en_US.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -locations relative
    SOURCE_TARGETS
        # 必须包含 app 目标本体：显式传 SOURCE_TARGETS 时 lupdate 只扫描列表内目标，
        # 遗漏主目标会把 QML 源串标 vanished、被 lrelease 丢弃（qm 缺 QML 上下文）
        ${MYNAME}
        FeatureSystemQml EditSystemQml AlgorithmSystemQml IOSystemQml
        FeatureDemoPluginlib MeasurePluginlib DimensionPluginlib MeshQualityPluginlib MeshRepairPluginlib
        CreateFacePluginlib DeleteFacePluginlib
        TetGenPluginlib TetGenLibPluginlib GmshPluginlib CmdExecutePluginlib
)

# 翻译检查：构建时校验 ts 无 vanished 条目（lupdate 扫描范围缩水的回归防护，
# vanished 条目会被 lrelease 丢弃导致 .qm 缺失 QML 上下文）
add_custom_target(check_translations
    COMMAND ${CMAKE_COMMAND} -DTS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/i18n/app_en_US.ts
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckTranslations.cmake
    COMMENT "检查 app_en_US.ts 中是否存在 vanished 条目"
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
