add_executable(coral_shared_preferences_test shared_preferences_test.cpp)
target_include_directories(coral_shared_preferences_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib ${CMAKE_SOURCE_DIR}/modules/foundation)
target_link_libraries(coral_shared_preferences_test PRIVATE coral::preferences coral_trace)
coral_target_common(coral_shared_preferences_test)
add_test(NAME shared_preferences_boundaries COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/checks/run_isolated.py
    --user-root ${CMAKE_CURRENT_BINARY_DIR}/users -- $<TARGET_FILE:coral_shared_preferences_test>)
set_tests_properties(shared_preferences_boundaries PROPERTIES TIMEOUT 30)
