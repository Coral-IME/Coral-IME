add_executable(zip_archive_test zip_archive_test.cpp)
target_link_libraries(zip_archive_test PRIVATE coral_archive)
coral_target_common(zip_archive_test)
add_test(NAME zip_archive_interop COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_zip.py
    $<TARGET_FILE:zip_archive_test> ${CMAKE_CURRENT_BINARY_DIR}/artifacts)
add_executable(namespace_peer namespace_peer.cpp)
target_link_libraries(namespace_peer PRIVATE coral_preferences)
coral_target_common(namespace_peer)
add_test(NAME configuration_namespace COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_namespace.py
    $<TARGET_FILE:namespace_peer>)
set_tests_properties(configuration_namespace PROPERTIES TIMEOUT 30)
