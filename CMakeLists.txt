cmake_minimum_required(VERSION 3.21)

project(coralpinyin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/coral_common.cmake)
include(cmake/product_identity.cmake)
add_library(coral_product_identity INTERFACE)
add_library(coral::product_identity ALIAS coral_product_identity)
target_include_directories(coral_product_identity INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT WIN32)
    message(FATAL_ERROR "本重构版本仅支持 Windows 构建；macOS/Linux/Android 见 PLATFORMS.md")
endif()

set(CMAKE_SYSTEM_VERSION "10.0.18362.0" CACHE STRING "Windows SDK version")

option(FUNCTIMER_OPEN "Enable function timer macro" OFF)

option(CORAL_BUILD_DEMO_DATA "Build self-authored demonstration data" ON)
option(CORAL_ENABLE_OFFICIAL_DATA_TESTS "Register full-dictionary tests (explicit external data required)" OFF)
set(CORAL_RUNTIME_DATA_DIR "" CACHE PATH "Optional external runtime root containing data/ and config/")
set(CORAL_CONFIG_DIR "${CMAKE_SOURCE_DIR}/resources/config")
if(CORAL_RUNTIME_DATA_DIR)
    foreach(required config/coralpinyinConfig.ini data/generallexicon/languagemodel.coral
            data/generallexicon/mainlexicon.coral data/generallexicon/english.marisa
            data/generallexicon/english.dat data/generallexicon/correction_pinyin_model.dat
            data/generallexicon/char_lm.dat)
        if(NOT EXISTS "${CORAL_RUNTIME_DATA_DIR}/${required}")
            message(FATAL_ERROR "CORAL_RUNTIME_DATA_DIR is incomplete: missing ${required}")
        endif()
    endforeach()
endif()
add_subdirectory(third_party/miniz)
add_library(coral_archive STATIC modules/foundation/zip_archive.cpp)
target_include_directories(coral_archive PUBLIC modules/foundation)
target_link_libraries(coral_archive PRIVATE coral_miniz)
coral_target_common(coral_archive)
include(CTest)
if(BUILD_TESTING OR CORAL_BUILD_DEMO_DATA)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
endif()

add_subdirectory(src/lib/spdlog)
# spdlog 自带上游 CMakeLists，不走 coral_target_common，显式挂同一组 MSVC 定义
# （上游脚本已含 /utf-8，这里只需定义宏；保持与旧全局定义一致的行为）
target_link_libraries(spdlog PRIVATE coral_msvc_defs)
add_subdirectory(modules/foundation)    # coral_trace 日志适配层（定义自 common/ 归位）
add_subdirectory(modules/preferences)   # coral_preferences 设置/配置库（定义自 common/ 归位）
add_library(coral_foundation INTERFACE)
add_library(coral::foundation ALIAS coral_foundation)
target_include_directories(coral_foundation INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation)
add_library(coral::trace ALIAS coral_trace)
add_library(coral::preferences ALIAS coral_preferences)
add_subdirectory(src/lib/marisa-trie)
add_subdirectory(src/lib/opencc)
# ---------- coral_domain：纯领域类型（Phase 5 首个目录迁移 target；无 Windows 依赖） ----------
add_library(coral_vocabulary INTERFACE)
add_library(coral::vocabulary ALIAS coral_vocabulary)
target_include_directories(coral_vocabulary INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(platform/windows/lexicon_io)
add_subdirectory(modules/assist/shape_input)
add_subdirectory(modules/assist/value_input)
add_subdirectory(modules/assist/symbol_input)
add_subdirectory(modules/storage)
add_subdirectory(modules/language_model)
add_subdirectory(modules/composition)
add_subdirectory(modules/phonetics)
add_subdirectory(tools/native/history_inspector)
add_subdirectory(tools/native/lexicon_builder)
add_subdirectory(tools/native/bundle_decoder)
add_subdirectory(tools/native/engine_console)
add_subdirectory(tools/native/parse_bench)
add_subdirectory(tools/native/lm_probe)
add_subdirectory(tools/native/resource_compiler)



add_subdirectory(platform/windows/diagnostics)
add_subdirectory(apps/diagnostic_collector)
add_subdirectory(src/platform/windows/ipc)
add_subdirectory(modules/transport)
add_subdirectory(modules/translation)
add_subdirectory(modules/updates)
add_subdirectory(modules/lexicon_catalog)
add_subdirectory(platform/windows/update_delivery)
add_subdirectory(platform/windows/session_host)
add_subdirectory(platform/windows/text_service)
add_subdirectory(platform/windows/host_startup)
add_subdirectory(src/platform/windows/renderer)
add_subdirectory(src/platform/windows/common/nanet)  # coralnet 网络库(libcurl)
add_subdirectory(modules/speech)
add_subdirectory(platform/windows/speech)
add_subdirectory(tools/native/speech_probe)
add_subdirectory(src/platform/windows/coraltsf)
add_subdirectory(platform/windows/service_launch)
add_subdirectory(apps/launcher_service)
add_subdirectory(apps/update_agent)
add_subdirectory(src/platform/windows/usersharedmemoryread)
add_subdirectory(src/platform/windows/coralpinyin)

# ---------- Phase 6 CTest 注册 ----------
# 引擎回归测试需要数据目录（含 data/ 与 config/，由数据包生成链产出）。
# 默认不注册正式数据测试；启用时须显式指定外置资源根目录。
set(CORAL_TEST_DATA_DIR "" CACHE PATH "External runtime root for opt-in official-data tests")
function(coral_isolated_test name target)
    add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py
        --user-root "${CMAKE_BINARY_DIR}/test-users/${name}" --data-dir "${CORAL_TEST_DATA_DIR}"
        -- $<TARGET_FILE:${target}> ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60 LABELS official_data)
endfunction()

if(BUILD_TESTING)
enable_testing()
add_subdirectory(tests/foundation)
add_subdirectory(tests/preferences)
add_subdirectory(tests/phonetics)
add_subdirectory(tests/speech)
add_subdirectory(tests/resource_io)
add_subdirectory(tests/language_model)
add_subdirectory(tests/composition)
if(CMAKE_CL_64 AND CORAL_BUILD_DEMO_DATA)
    add_test(NAME static_core_linkage COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/checks/verify_engine_linkage.py
        $<TARGET_FILE:coral_engine_host> $<TARGET_FILE:coralpinyin> $<TARGET_FILE:coral_resource_compiler>
        $<TARGET_FILE:coral_demo_generate>)
endif()
if(CORAL_BUILD_MIXED_LANGUAGE_TEST)
    add_subdirectory(tests/mixed_language)
endif()

add_subdirectory(tests/uvmode)

# Portable UTF-8 doublepinyin metadata regression test.
add_subdirectory(tests/doublepinyin)
add_subdirectory(tests/app_version)
add_subdirectory(tests/digit_punctuation)
if(WIN32)
    add_subdirectory(tests/settings_ui)
    add_subdirectory(tests/fuzzy_dialog)
    add_subdirectory(tests/lexicon_import_dialog)
endif()

# CSW4 session wire contract and Windows pipe failure behavior.
add_subdirectory(tests/session_wire)
add_subdirectory(tests/text_service)
add_subdirectory(tests/host_startup)
add_subdirectory(tests/service_launch)
add_subdirectory(tests/diagnostics)
add_subdirectory(tests/updates)
add_subdirectory(tests/lexicon_catalog)
add_test(NAME session_wire COMMAND session_wire_test)
add_test(NAME pipe_session_client COMMAND pipe_session_client_test)

# M4 renderer snapshot/action wire contract.
add_subdirectory(tests/renderer_protocol)
add_test(NAME renderer_protocol_v3 COMMAND renderer_protocol_v3_test)

# M1 TSF state-kernel tests are data-independent and intentionally small.
option(CORAL_BUILD_TSF_STATE_TEST "Build TSF state-kernel tests" ON)
if(CORAL_BUILD_TSF_STATE_TEST)
    add_subdirectory(tests/translation)
    add_subdirectory(tests/tsf_state)
    add_test(NAME tsf_state COMMAND tsf_state_test)
    add_test(NAME tsf_sink_contract COMMAND tsf_sink_contract_test)
    add_test(NAME tsf_host_smoke COMMAND tsf_host_smoke)
endif()

# User-history format safety test is data-independent and always cheap.
add_subdirectory(tests/user_history)
add_test(NAME user_history_codec COMMAND user_history_codec_test)

if(WIN32)
    add_subdirectory(tests/ipc)
endif()

# Static settings-page wiring check; PowerShell is available on supported Windows.
add_test(NAME settings_ui_wiring
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
        ${CMAKE_SOURCE_DIR}/tests/settings_ui/settings_ui_wiring.ps1)

# ---------- Phase 4 按键序列测试 ----------
option(CORAL_BUILD_SEQUENCE_TEST "Build coral::composition session key-sequence test" ON)
if(CORAL_BUILD_SEQUENCE_TEST)
    add_subdirectory(tests/sequence)
endif()
if(CORAL_ENABLE_OFFICIAL_DATA_TESTS AND CORAL_BUILD_SEQUENCE_TEST AND TARGET sequence_test)
    coral_isolated_test(sequence sequence_test @USER_DIR@)
endif()

# ---------- 用户词库导入（自动注音 / 查重 / 写入 / 撤销） ----------
option(CORAL_BUILD_LEXICON_IMPORT_TEST "Build user-lexicon import engine test" ON)
if(CORAL_BUILD_LEXICON_IMPORT_TEST)
    add_subdirectory(tests/lexicon_import_engine)
    if(CORAL_ENABLE_OFFICIAL_DATA_TESTS)
    coral_isolated_test(lexicon_import_engine lexicon_import_engine_test "${CORAL_TEST_DATA_DIR}")
    endif()
endif()

# ---------- Phase 6 Step 4 配置门闸测试 ----------
option(CORAL_BUILD_CONFIG_GATE_TEST "Build cross-process config gate test" ON)
if(CORAL_BUILD_CONFIG_GATE_TEST)
    add_subdirectory(tests/config_gate)
endif()
if(CORAL_ENABLE_OFFICIAL_DATA_TESTS AND CORAL_BUILD_CONFIG_GATE_TEST AND TARGET config_gate_test)
    coral_isolated_test(config_gate config_gate_test "${CORAL_TEST_DATA_DIR}")
endif()

add_subdirectory(tests/archive)
endif()
if(CORAL_ENABLE_OFFICIAL_DATA_TESTS AND NOT EXISTS "${CORAL_TEST_DATA_DIR}/config/coralpinyinConfig.ini")
    message(FATAL_ERROR "Official-data tests need CORAL_TEST_DATA_DIR/config/coralpinyinConfig.ini")
endif()
if(CORAL_BUILD_DEMO_DATA)
    add_subdirectory(samples/demo)
endif()
