add_executable(session_wire_test session_wire_test.cpp)
target_link_libraries(session_wire_test PRIVATE coral::session_wire)
coral_target_common(session_wire_test)

add_executable(pipe_session_client_test pipe_session_client_test.cpp)
target_link_libraries(pipe_session_client_test PRIVATE coral_pipe_session_client)
coral_target_common(pipe_session_client_test)

add_executable(session_host_smoke session_host_smoke.cpp)
target_link_libraries(session_host_smoke PRIVATE coral_pipe_session_client)
coral_target_common(session_host_smoke)

if(TARGET coral_engine_host AND CORAL_BUILD_DEMO_DATA)
    add_executable(coral_host_process_probe host_process_probe.cpp)
    target_link_libraries(coral_host_process_probe PRIVATE coral::pipe_session_client coral::session_bridge coral::host_bootstrap)
    target_include_directories(coral_host_process_probe PRIVATE ${CMAKE_SOURCE_DIR})
    coral_target_common(coral_host_process_probe)
    add_test(NAME session_host_process COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_host_process.py
        $<TARGET_FILE:coral_engine_host> $<TARGET_FILE:coral_host_process_probe>
        ${CMAKE_BINARY_DIR}/demo-runtime ${CMAKE_BINARY_DIR}/test-users/host-process-$<CONFIG>)
    set_tests_properties(session_host_process PROPERTIES TIMEOUT 90 LABELS demo)
endif()
