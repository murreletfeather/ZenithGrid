# 校验应用的 .ts 翻译源文件：translation type="vanished" 的条目必须为 0。
# vanished 条目会被 lrelease 从 .qm 中丢弃，通常因 lupdate 扫描范围缩水
#（例如 qt_add_translations 显式指定 SOURCE_TARGETS 却遗漏了主目标）或源串被删除而产生。
# 用法（由 app/CMakeLists.txt 的 check_translations 自定义目标调用）：
#   cmake -DTS_FILE=<path>/app_en_US.ts -P cmake/CheckTranslations.cmake
if(NOT DEFINED TS_FILE)
    message(FATAL_ERROR "CheckTranslations: TS_FILE 未指定")
endif()
if(NOT EXISTS "${TS_FILE}")
    message(FATAL_ERROR "CheckTranslations: 找不到翻译源文件: ${TS_FILE}")
endif()

file(READ "${TS_FILE}" _ts_content)
string(REGEX MATCHALL "type=\"vanished\"" _vanished_matches "${_ts_content}")
string(REGEX MATCHALL "type=\"unfinished\"" _unfinished_matches "${_ts_content}")

if(_vanished_matches)
    list(LENGTH _vanished_matches _vanished_count)
    message(FATAL_ERROR
        "CheckTranslations: 发现 ${_vanished_count} 条 vanished 翻译条目。\n"
        "  通常原因：lupdate 扫描范围缩水（检查 qt_add_translations 的 SOURCE_TARGETS 是否遗漏主目标）\n"
        "  或源串已被删除。请先运行: cmake --build <build-dir> --target update_translations\n"
        "  并确认这些条目在源码中仍然存在；若源串确已删除，请经 Qt Linguist 清理后重试。")
endif()

list(LENGTH _unfinished_matches _unfinished_count)
message(STATUS "CheckTranslations: 通过（vanished=0，unfinished=${_unfinished_count}）")
