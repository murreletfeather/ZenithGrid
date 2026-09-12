find_package(KDDockWidgets-qt6 REQUIRED)

add_subdirectory(core)
add_subdirectory(model)
add_subdirectory(render)

# 1) 生成 App Bundle（macOS 上的 .app 目录结构）
set(app_resource "")
if(WIN32)
    set(app_resource "${CMAKE_SOURCE_DIR}/resource/PreCess.rc")
endif()
qt_add_executable(${MYNAME} MACOSX_BUNDLE main.cpp ${app_resource})
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
            AttributeRenderPanel.qml
            CentralRenderArea.qml
            SideBar.qml
            ObjectTree.qml
            Selector.qml
            JavaScriptConsole.qml
            PythonConsole.qml
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
    Qt${VTK_QT_VERSION}::Svg
    KDAB::kddockwidgets
    spdlog::spdlog
    "$<TARGET_NAME_IF_EXISTS:StaticPlugins>"
)

target_compile_definitions(${MYNAME} PRIVATE
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>"
)

# - Prevent the creation of a console window on Windows.
# - Create an application bundle on macOS.
set_target_properties(${MYNAME} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(APPLE)
    set(app_icon_macos "${CMAKE_SOURCE_DIR}/resource/PreCess.icns")
    target_sources(${MYNAME} PRIVATE ${app_icon_macos})
    set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(${MYNAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE PreCess.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER com.precess.PreCess
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

if(EMSCRIPTEN)
target_sources(${MYNAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/QWasmBridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/QWasmBridge.h"
)
# 静态 wasm 构建需要显式导入图片格式插件
qt_import_plugins(${MYNAME} INCLUDE
    Qt6::QSvgPlugin
    Qt6::QSvgIconPlugin
    Qt6::QJpegPlugin
    Qt6::QGifPlugin
    Qt6::QICOPlugin
)
# OCCT 8.0 起 BRepMesh_IncrementalMeshFactory 使用静态初始化器注册，
# 但会被 wasm 链接器优化掉，这里强制链接 TKMesh 静态库解决。
target_link_options(${MYNAME} PRIVATE
    "LINKER:--whole-archive"
    "$<TARGET_FILE:TKMesh>"
    "LINKER:--no-whole-archive"
)
endif()

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
#    - wasm 全静态链接，无动态库依赖扫描与 QML 部署脚本
if(NOT EMSCRIPTEN)
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
# 内嵌 Python 时把构建所用解释器目录（python/ 导出的缓存变量）加入依赖搜索，
# 使 install 能收拢解释器运行库
if(PRECESS_PYTHON_HOME_DIR)
    list(APPEND DEPENDENCY_DIRECTORIES "${PRECESS_PYTHON_HOME_DIR}")
endif()
set(DEPENDENCY_POST_EXCLUDE_REGEXES ".*system32/.*\\.dll")
if(APPLE)
    list(APPEND DEPENDENCY_POST_EXCLUDE_REGEXES
        "/usr/lib/.*"
        "/System/Library/.*"
    )
elseif(UNIX)
    list(APPEND DEPENDENCY_POST_EXCLUDE_REGEXES
        ".*libc\\.so.*"
        ".*libm\\.so.*"
        ".*libdl\\.so.*"
        ".*libpthread\\.so.*"
        ".*librt\\.so.*"
        ".*libnsl\\.so.*"
        ".*libstdc\\+\\+\\.so.*"
        ".*libgcc_s\\.so.*"
        ".*libGL\\.so.*"
        ".*libEGL\\.so.*"
        ".*libOpenGL\\.so.*"
        ".*libGLX\\.so.*"
        ".*libxkbcommon\\.so.*"
        ".*libwayland-.*\\.so.*"
        ".*libX[a-zA-Z0-9]*\\.so.*"
        ".*libfontconfig\\.so.*"
        ".*libglib-2\\.0\\.so.*"
    )
endif()
install(
    TARGETS ${MYNAME}
    RUNTIME_DEPENDENCIES
    DIRECTORIES ${DEPENDENCY_DIRECTORIES}
    PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-" # don't install system provided libs
    POST_EXCLUDE_REGEXES ${DEPENDENCY_POST_EXCLUDE_REGEXES}
    FRAMEWORK DESTINATION $<IF:$<PLATFORM_ID:Darwin>,${MYNAME}.app/Contents/Frameworks,Frameworks>
    LIBRARY DESTINATION $<IF:$<PLATFORM_ID:Darwin>,${MYNAME}.app/Contents/Frameworks,lib>
    RUNTIME DESTINATION $<IF:$<PLATFORM_ID:Darwin>,${MYNAME}.app/Contents/MacOS,bin>
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
endif()

# 5) 使用 fixup_bundle 将所有依赖“收拢”进 .app，并修正引用路径（仅在 macOS 下执行）
if(APPLE)
  install(CODE "
    include(BundleUtilities)
    # 使用 $ENV{DESTDIR} 拼接最终路径
    set(BUNDLE_PATH \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MYNAME}.app/Contents/MacOS/${MYNAME}\")
    # 插件经运行期加载、不在主程序链接依赖内，一并交给 fixup_bundle 解析依赖并改写引用
    file(GLOB plugin_libraries
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MYNAME}.app/Contents/plugins/*.dylib\")
    message(STATUS \"fixup_bundle: processing \${BUNDLE_PATH}\")
    fixup_bundle(
      \"\${BUNDLE_PATH}\"
      \"\${plugin_libraries}\"
      \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${MYNAME}.app/Contents/Frameworks;\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/lib;/opt/homebrew/lib;/usr/local/lib\"
    )
  ")
endif()

# 6) 构建 wasm 平台，捆绑字体，生成 HTML 壳，并安装网页侧全部产物
if(EMSCRIPTEN)
set(APPNAME ${MYNAME})
# 与 Qt6WasmMacros 的默认导出名规则保持一致（<target>_entry）
string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" wasm_target_name "${MYNAME}")
set(APPEXPORTNAME "${wasm_target_name}_entry")
string(TIMESTAMP CACHE_ID "%Y%m%d%H%M%S" UTC)
configure_file("${CMAKE_SOURCE_DIR}/resource/pwa/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/resource/pwa/sw.js" "${CMAKE_CURRENT_BINARY_DIR}/sw.js" @ONLY)

# 捆绑字体资源，生成 qrc:/fonts/appfont.bin
set(WASM_FONT_FILE "" CACHE FILEPATH "嵌入资源的字体文件（默认使用思源黑体）")
if(NOT WASM_FONT_FILE)
    if(EXISTS "${WASM_QT_DIR}/../fonts/SourceHanSansCN-Regular.otf")
        set(WASM_FONT_FILE "${WASM_QT_DIR}/../fonts/SourceHanSansCN-Regular.otf")
    endif()
endif()
if(WASM_FONT_FILE)
    message(STATUS "wasm use font: ${WASM_FONT_FILE}")
    set(WASM_FONT_EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/appfont.bin")
    file(COPY_FILE "${WASM_FONT_FILE}" "${WASM_FONT_EMBEDDED}")
    set_source_files_properties("${WASM_FONT_EMBEDDED}" PROPERTIES
        QT_RESOURCE_ALIAS "appfont.bin")
    qt_add_resources(${MYNAME} "appfont"
        FILES "${WASM_FONT_EMBEDDED}"
        PREFIX "/fonts"
    )
else()
    message(WARNING "未找到可嵌入的字体，中文将显示为方框，请将 WASM_FONT_FILE 设置为指定的字体路径")
endif()

# 安装其他产物
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qtloader.js"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${MYNAME}.wasm"
    "${CMAKE_CURRENT_BINARY_DIR}/${MYNAME}.data"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
    OPTIONAL
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${MYNAME}.worker.js"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
    OPTIONAL
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/index.html"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../resource/PreCess.ico"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
)
install(FILES
    "${CMAKE_SOURCE_DIR}/resource/pwa/manifest.json"
    "${CMAKE_CURRENT_BINARY_DIR}/sw.js"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${MYNAME}
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resource/pwa/icons/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/icons"
    COMPONENT ${MYNAME}
)
endif()
