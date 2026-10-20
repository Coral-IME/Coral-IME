add_executable(composition_contract_test composition_contract_test.cpp)
target_link_libraries(composition_contract_test PRIVATE coral::composition)
coral_target_common(composition_contract_test)
add_test(NAME composition_contract COMMAND composition_contract_test)

if(TARGET coral_engine_core AND CORAL_BUILD_DEMO_DATA)
    add_executable(local_session_port_test local_session_port_test.cpp)
    target_include_directories(local_session_port_test PRIVATE ../../modules/runtime ../../modules/foundation
        ../../modules/preferences ../../modules/phonetics/script ../../src/common ../../src/lib
        ../../modules/assist/shape_input ../../modules/assist/value_input ../../modules/assist/symbol_input
        ../../src/lib/marisa-trie/include ../../src/lib/opencc/deps/rapidjson-1.1.0)
    target_link_libraries(local_session_port_test PRIVATE coral_engine_core)
    target_compile_definitions(local_session_port_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
    coral_target_common(local_session_port_test)
    add_test(NAME local_session_port COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py
        --user-root ${CMAKE_BINARY_DIR}/test-users/local-session --data-dir ${CMAKE_BINARY_DIR}/demo-runtime
        -- $<TARGET_FILE:local_session_port_test> ${CMAKE_BINARY_DIR}/demo-runtime @USER_DIR@)
    set_tests_properties(local_session_port PROPERTIES TIMEOUT 30 LABELS demo)
endif()
