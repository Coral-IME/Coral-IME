add_executable(coral_curl_lifecycle_test curl_lifecycle_test.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/windows/common/nanet/src/coralnet.cpp
    ${CMAKE_SOURCE_DIR}/platform/windows/update_delivery/update_http.cpp)
target_include_directories(coral_curl_lifecycle_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/platform/windows/common/nanet/include
    ${CMAKE_SOURCE_DIR}/platform/windows/update_delivery)
# 只替换测试目标的入口；实际句柄和全局状态仍由同一份 libcurl 提供。
target_compile_definitions(coral_curl_lifecycle_test PRIVATE
    curl_global_init=coral_test_global_init curl_global_cleanup=coral_test_global_cleanup
    curl_easy_init=coral_test_easy_init curl_multi_init=coral_test_multi_init
    curl_easy_escape=coral_test_easy_escape curl_easy_unescape=coral_test_easy_unescape)
target_link_libraries(coral_curl_lifecycle_test PRIVATE libcurl_static)
coral_target_common(coral_curl_lifecycle_test)
add_test(NAME curl_lifecycle COMMAND coral_curl_lifecycle_test)
set_tests_properties(curl_lifecycle PROPERTIES TIMEOUT 15)
