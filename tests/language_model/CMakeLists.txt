add_executable(surface_model_test surface_model_test.cpp)
target_link_libraries(surface_model_test PRIVATE coral::language_model)
target_compile_features(surface_model_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(surface_model_test PRIVATE /utf-8)
endif()
add_test(NAME surface_model_format COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_format.py $<TARGET_FILE:coral_lm_probe>
    $<TARGET_FILE:surface_model_test>)
set_tests_properties(surface_model_format PROPERTIES TIMEOUT 60)
add_test(NAME character_model COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_character_model.py $<TARGET_FILE:coral_character_probe>)
set_tests_properties(character_model PROPERTIES TIMEOUT 60)
if(TARGET coral_engine_core)
    add_executable(surface_decoder_test surface_decoder_test.cpp)
    target_link_libraries(surface_decoder_test PRIVATE coral_engine_core)
    target_include_directories(surface_decoder_test PRIVATE
        ../../src/common ../../src/lib ../../src/lib/marisa-trie/include
        ../../modules/foundation ../../modules/preferences ../../modules/phonetics/script
        ../../src/lib/opencc/deps/rapidjson-1.1.0)
    target_compile_definitions(surface_decoder_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
    coral_target_common(surface_decoder_test)
    add_test(NAME surface_decoder COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py
        --user-root "${CMAKE_BINARY_DIR}/test-users/surface_decoder"
        -- ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_format.py
        $<TARGET_FILE:coral_lm_probe> $<TARGET_FILE:surface_model_test> $<TARGET_FILE:surface_decoder_test> $<BOOL:${CORAL_ENABLE_CHARACTER_MODEL}>)
    set_tests_properties(surface_decoder PROPERTIES TIMEOUT 60)
endif()
