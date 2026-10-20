add_executable(coral_demo_generate generate.cpp)
target_include_directories(coral_demo_generate PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/phonetics ${CMAKE_SOURCE_DIR}/modules/phonetics
    ${CMAKE_SOURCE_DIR}/modules/foundation ${CMAKE_SOURCE_DIR}/modules/preferences
    ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/lib)
target_link_libraries(coral_demo_generate PRIVATE coral_engine_core marisa-trie)
target_compile_definitions(coral_demo_generate PRIVATE OS_WINDOW)
coral_target_common(coral_demo_generate)
set(CORAL_DEMO_DATA_DIR "${CMAKE_BINARY_DIR}/demo-runtime")
file(GLOB_RECURSE demo_inputs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures*" "${CORAL_CONFIG_DIR}/*")
add_custom_command(OUTPUT "${CORAL_DEMO_DATA_DIR}/demo.stamp"
    COMMAND ${CMAKE_COMMAND} -E env "CORAL_TEST_NAMESPACE=demo_generator"
        "CORAL_USER_DATA_DIR=${CMAKE_BINARY_DIR}/demo-generator-user"
        $<TARGET_FILE:coral_demo_generate> "${CMAKE_CURRENT_SOURCE_DIR}/fixtures" "${CORAL_DEMO_DATA_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/finalize.py"
        "${CORAL_DEMO_DATA_DIR}" "${CORAL_CONFIG_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    DEPENDS coral_demo_generate coral_lexicon_builder ${demo_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/finalize.py"
    VERBATIM)
add_custom_target(coral_demo_data ALL DEPENDS "${CORAL_DEMO_DATA_DIR}/demo.stamp")
if(BUILD_TESTING)
    add_executable(coral_demo_smoke smoke.cpp)
    target_include_directories(coral_demo_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/phonetics ${CMAKE_SOURCE_DIR}/modules/phonetics
        ${CMAKE_SOURCE_DIR}/modules/foundation ${CMAKE_SOURCE_DIR}/modules/preferences
        ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/lib)
    target_link_libraries(coral_demo_smoke PRIVATE coral_engine_core)
    target_compile_definitions(coral_demo_smoke PRIVATE OS_WINDOW)
    coral_target_common(coral_demo_smoke)
    add_dependencies(coral_demo_smoke coral_demo_data)
    add_test(NAME demo_engine COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py"
        --user-root "${CMAKE_BINARY_DIR}/test-users/demo" --data-dir "${CORAL_DEMO_DATA_DIR}"
        -- $<TARGET_FILE:coral_demo_smoke> "${CORAL_DEMO_DATA_DIR}" @USER_DIR@)
    set_tests_properties(demo_engine PROPERTIES TIMEOUT 45 LABELS demo)
endif()
