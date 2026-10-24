# coralpinyin：CORAL 拼音主程序（Qt-free，Win32 原生 UI + WTL）

add_executable(coralpinyin
    coralpinyin.rc
    app_version.rc
    pch.cpp

    src/main.cpp
    src/pinyin.cpp
    src/engineserver.cpp
    src/apphost.cpp
    src/clipboardhistory.cpp

    src/ui/settingmenu.cpp
    src/ui/settingswindow.cpp
    src/ui/skincropdialog.cpp
    src/ui/confirmdialog.cpp
    src/ui/fuzzypinyindialog.cpp
    src/ui/lexiconimportdialog.cpp
    src/ui/addphrasedialog.cpp
    ${CMAKE_SOURCE_DIR}/apps/settings_app/update_coordinator.cpp
    src/ui/updatewindow.cpp
    ${CMAKE_SOURCE_DIR}/apps/settings_app/online_lexicon_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/apps/settings_app/other_ime_import_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/apps/settings_app/default_input_settings.cpp

    ../common/skingui/wndeffect.cpp
    ../../../../modules/foundation/product_constants.cpp
)

target_include_directories(coralpinyin PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/settings_app
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../modules/phonetics
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../modules/phonetics
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/nanet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/opencc/deps/rapidjson-1.1.0
)
# 程序版本：可通过 -DCORAL_APP_VERSION=x.y.z.w 覆盖，默认 1.0.0.5
if(NOT DEFINED CORAL_APP_VERSION)
    set(CORAL_APP_VERSION "1.0.0.5")
endif()
if(NOT CORAL_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "CORAL_APP_VERSION must be four numeric components: ${CORAL_APP_VERSION}")
endif()
string(REPLACE "." ";" _coral_app_ver_list "${CORAL_APP_VERSION}")
list(LENGTH _coral_app_ver_list _coral_app_ver_len)
if(NOT _coral_app_ver_len EQUAL 4)
    message(FATAL_ERROR "CORAL_APP_VERSION 必须是 a.b.c.w 四段式，实际为: ${CORAL_APP_VERSION}")
endif()
foreach(_part IN LISTS _coral_app_ver_list)
    if(_part GREATER 65535)
        message(FATAL_ERROR "CORAL_APP_VERSION components must be <= 65535: ${CORAL_APP_VERSION}")
    endif()
endforeach()
list(GET _coral_app_ver_list 0 _ver_a)
list(GET _coral_app_ver_list 1 _ver_b)
list(GET _coral_app_ver_list 2 _ver_c)
list(GET _coral_app_ver_list 3 _ver_d)
target_compile_definitions(coralpinyin PRIVATE
    CORAL_APP_VER_MS=${_ver_a},${_ver_b}
    CORAL_APP_VER_LS=${_ver_c},${_ver_d}
    CORAL_APP_VER_STR=\"${CORAL_APP_VERSION}\"
)
target_compile_definitions(coralpinyin PRIVATE OS_WINDOW _WIN32 _UNICODE)
if(FUNCTIMER_OPEN)
    target_compile_definitions(coralpinyin PRIVATE FUNCTIMER_OPEN)
endif()
target_link_libraries(coralpinyin PRIVATE
    coralnet
    coral::diagnostics
    coral::update_delivery
    coral::lexicon_io
    coral::lexicon_catalog
    coral::windows_speech
    coral_engine_core
    opencc marisa-trie
    NetApi32 version UIAutomationCore
)

target_link_libraries(coralpinyin PRIVATE coralipc coral_trace coral_preferences)
# 外观页的候选窗皮肤预览直接复用候选窗绘制。
target_link_libraries(coralpinyin PRIVATE coral_candidate_view)

target_precompile_headers(coralpinyin PRIVATE pch.h)

coral_target_common(coralpinyin)
coral_product_output(coralpinyin CORAL_ID_SETTINGS_EXE)

# 独立迁移脚本随开发输出与安装包一起提供，不复制任何用户词库。
add_custom_target(coral_stage_ime_migration ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:coralpinyin>/tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/tools/data/ime_dictionary_tool.py"
        "$<TARGET_FILE_DIR:coralpinyin>/tools/ime_dictionary_tool.py")
add_dependencies(coralpinyin coral_stage_ime_migration)


# GUI/Win 程序：使用 Windows 子系统，避免运行/打包时弹出控制台黑窗
set_target_properties(coralpinyin PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_options(coralpinyin PRIVATE /ENTRY:mainCRTStartup)
