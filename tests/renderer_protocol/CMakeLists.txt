add_executable(renderer_protocol_v3_test renderer_protocol_v3_test.cpp)
target_include_directories(renderer_protocol_v3_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/windows/renderer
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(renderer_protocol_v3_test PRIVATE coral_renderer_protocol)
coral_target_common(renderer_protocol_v3_test)

add_executable(renderer_port_smoke renderer_port_smoke.cpp)
target_include_directories(renderer_port_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/windows/renderer
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(renderer_port_smoke PRIVATE coral_renderer_protocol)
coral_target_common(renderer_port_smoke)

if(CMAKE_CL_64)
    add_executable(renderer_window_state_test renderer_window_state_test.cpp)
    target_compile_definitions(renderer_window_state_test PRIVATE OS_WINDOW _UNICODE UNICODE)
    target_include_directories(renderer_window_state_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/windows/speech)
    target_link_libraries(renderer_window_state_test PRIVATE
        coral_renderer_protocol coral_candidate_view coral_candidate_backdrop coral_preferences coral::foundation User32 Gdi32 Advapi32 Userenv)
    coral_target_common(renderer_window_state_test)
    add_test(NAME renderer_window_state COMMAND renderer_window_state_test)

    add_executable(renderer_pipe_server_test renderer_pipe_server_test.cpp)
    target_compile_definitions(renderer_pipe_server_test PRIVATE OS_WINDOW _UNICODE UNICODE)
    target_include_directories(renderer_pipe_server_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/windows/speech)
    target_link_libraries(renderer_pipe_server_test PRIVATE
        coral_renderer_protocol coral_candidate_view coral_candidate_backdrop coral_preferences coral::foundation User32 Gdi32 Advapi32 Userenv)
    coral_target_common(renderer_pipe_server_test)
    add_test(NAME renderer_pipe_server COMMAND renderer_pipe_server_test)
    set_tests_properties(renderer_pipe_server PROPERTIES TIMEOUT 20)
endif()
