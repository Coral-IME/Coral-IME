add_executable(mixed_language_test mixed_language_test.cpp
    ../../modules/storage/resource_table_builder.cpp)
target_include_directories(mixed_language_test PRIVATE
    ../../modules/phonetics
    ../../src/common
    ../../src/lib
    ../../src/lib/marisa-trie/include
    ../../modules/foundation
    ../../modules/preferences
    ../../modules/phonetics/script
    ../../src/lib/opencc/deps/rapidjson-1.1.0)
target_compile_definitions(mixed_language_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
target_link_libraries(mixed_language_test PRIVATE coral_engine_core)
add_dependencies(mixed_language_test coral_lexicon_builder coral_bundle_decoder)
coral_target_common(mixed_language_test)
add_test(NAME mixed_language COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py
    --user-root "${CMAKE_BINARY_DIR}/test-users/mixed_language"
    -- $<TARGET_FILE:mixed_language_test>)
set_tests_properties(mixed_language PROPERTIES TIMEOUT 60)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME mixed_language_pipeline COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_pipeline.py $<TARGET_FILE:coral_lexicon_builder>)
    set_tests_properties(mixed_language_pipeline PROPERTIES TIMEOUT 60)
endif()
