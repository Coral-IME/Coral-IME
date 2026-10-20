add_executable(resource_io_test resource_io_test.cpp)
target_link_libraries(resource_io_test PRIVATE coral_resource_io)
coral_target_common(resource_io_test)
add_test(NAME resource_io COMMAND resource_io_test ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
set_tests_properties(resource_io PROPERTIES TIMEOUT 30)

if(TARGET coral_engine_core AND CORAL_BUILD_DEMO_DATA)
    add_executable(engine_loading_test engine_loading_test.cpp)
    target_include_directories(engine_loading_test PRIVATE
        ../../modules/foundation ../../modules/preferences ../../modules/phonetics/script ../../src/common ../../src/lib
        ../../modules/assist/shape_input ../../modules/assist/value_input ../../modules/assist/symbol_input
        ../../src/lib/marisa-trie/include ../../src/lib/opencc/deps/rapidjson-1.1.0)
    target_link_libraries(engine_loading_test PRIVATE coral_engine_core)
    target_compile_definitions(engine_loading_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
    coral_target_common(engine_loading_test)
    add_test(NAME engine_loading COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py
        --user-root ${CMAKE_BINARY_DIR}/test-users/loading --data-dir ${CMAKE_BINARY_DIR}/demo-runtime
        -- $<TARGET_FILE:engine_loading_test> ${CMAKE_BINARY_DIR}/demo-runtime @USER_DIR@)
    set_tests_properties(engine_loading PROPERTIES TIMEOUT 30 LABELS demo)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(core_shutdown_test core_shutdown_test.cpp)
        target_include_directories(core_shutdown_test PRIVATE
            ../../modules/foundation ../../modules/preferences ../../modules/phonetics/script ../../src/common ../../src/lib
            ../../modules/assist/shape_input ../../modules/assist/value_input ../../modules/assist/symbol_input
            ../../src/lib/marisa-trie/include ../../src/lib/opencc/deps/rapidjson-1.1.0)
        target_link_libraries(core_shutdown_test PRIVATE coral::engine_core)
        target_compile_definitions(core_shutdown_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
        coral_target_common(core_shutdown_test)
        add_executable(coral_runtime_restart_test runtime_restart_test.cpp)
        get_target_property(CORAL_RESTART_INCLUDE_DIRS core_shutdown_test INCLUDE_DIRECTORIES)
        target_include_directories(coral_runtime_restart_test PRIVATE ${CORAL_RESTART_INCLUDE_DIRS})
        target_link_libraries(coral_runtime_restart_test PRIVATE coral::engine_core)
        target_compile_definitions(coral_runtime_restart_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
        coral_target_common(coral_runtime_restart_test)
        add_test(NAME runtime_restart COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_runtime_restart.py
            $<TARGET_FILE:coral_runtime_restart_test> ${CMAKE_BINARY_DIR}/demo-runtime ${CMAKE_BINARY_DIR}/test-users/runtime-restart)
        set_tests_properties(runtime_restart PROPERTIES TIMEOUT 40 LABELS demo)
        add_test(NAME core_shutdown COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_core_shutdown.py
            $<TARGET_FILE:core_shutdown_test> ${CMAKE_BINARY_DIR}/demo-runtime ${CMAKE_BINARY_DIR}/test-users/core-shutdown)
        set_tests_properties(core_shutdown PROPERTIES TIMEOUT 35 LABELS demo)
    endif()
endif()
