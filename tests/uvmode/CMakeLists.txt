add_executable(uvmode_test uvmode_test.cpp
    ${CMAKE_SOURCE_DIR}/modules/phonetics/candidate_result.cpp)
target_include_directories(uvmode_test PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/phonetics
    ${CMAKE_SOURCE_DIR}/modules/phonetics
    ${CMAKE_SOURCE_DIR}/modules/foundation
    ${CMAKE_SOURCE_DIR}/modules/preferences
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/lib
    ${CMAKE_SOURCE_DIR}/src/lib/marisa-trie/include)
target_link_libraries(uvmode_test PRIVATE vmodeprocessor coral_trace coral_preferences coral_vocabulary)
coral_target_common(uvmode_test)
add_test(NAME uvmode COMMAND uvmode_test)

add_executable(uvmode_data_test uvmode_data_test.cpp)
target_link_libraries(uvmode_data_test PRIVATE coral_foundation)
target_include_directories(uvmode_data_test PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/phonetics
    ${CMAKE_SOURCE_DIR}/modules/assist/shape_input)
coral_target_common(uvmode_data_test)
add_test(NAME uvmode_data COMMAND uvmode_data_test ${CMAKE_CURRENT_BINARY_DIR}/test-data)

add_executable(uvmode_engine_test uvmode_engine_test.cpp)
target_include_directories(uvmode_engine_test PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/phonetics
    ${CMAKE_SOURCE_DIR}/modules/foundation
    ${CMAKE_SOURCE_DIR}/modules/preferences
    ${CMAKE_SOURCE_DIR}/src/common)
target_link_libraries(uvmode_engine_test PRIVATE coral_engine_core Psapi)
coral_target_common(uvmode_engine_test)
target_compile_definitions(uvmode_engine_test PRIVATE OS_WINDOW)
if(CORAL_ENABLE_OFFICIAL_DATA_TESTS AND EXISTS "${CORAL_TEST_DATA_DIR}/config/coralpinyinConfig.ini")
    coral_isolated_test(uvmode_engine uvmode_engine_test "${CORAL_TEST_DATA_DIR}" @USER_DIR@)
endif()

add_executable(uvmode_view_test uvmode_view_test.cpp)
target_link_libraries(uvmode_view_test PRIVATE coral_candidate_view)
coral_target_common(uvmode_view_test)
add_test(NAME uvmode_view COMMAND uvmode_view_test ${CMAKE_CURRENT_BINARY_DIR}/view-artifacts)
