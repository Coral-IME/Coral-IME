add_executable(coral_lexicon_service_test service_endpoint_test.cpp)
target_link_libraries(coral_lexicon_service_test PRIVATE coral::lexicon_catalog)
coral_target_common(coral_lexicon_service_test)
target_compile_definitions(coral_lexicon_service_test PRIVATE CORAL_EXPECT_LEXICON_BASE="${CORAL_LEXICON_SERVICE_BASE_URL}")
add_test(NAME lexicon_service COMMAND coral_lexicon_service_test)
add_executable(coral_online_lexicon_test online_lexicon_test.cpp)
target_include_directories(coral_online_lexicon_test PRIVATE support ${CMAKE_SOURCE_DIR}/apps/settings_app
    ${CMAKE_SOURCE_DIR}/src/platform/windows/common/nanet/include ${CMAKE_SOURCE_DIR}/src/lib/opencc/deps/rapidjson-1.1.0)
target_link_libraries(coral_online_lexicon_test PRIVATE coral::lexicon_catalog bcrypt)
coral_target_common(coral_online_lexicon_test)
add_test(NAME online_lexicon_lifecycle COMMAND coral_online_lexicon_test ${CMAKE_BINARY_DIR}/test-users/online-lexicon-$<CONFIG>)
add_test(NAME lexicon_service_configuration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_service_configuration.py
    ${CMAKE_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/service_configuration.cmake ${CMAKE_BINARY_DIR}/test-users/lexicon-config-$<CONFIG>)
set_tests_properties(lexicon_service online_lexicon_lifecycle lexicon_service_configuration PROPERTIES TIMEOUT 30)
