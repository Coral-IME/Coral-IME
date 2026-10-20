add_library(coral_pipe_session_client STATIC
    pipe_session_client.cpp
    pipe_session_client.h)
add_library(coral::pipe_session_client ALIAS coral_pipe_session_client)
target_include_directories(coral_pipe_session_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(coral_pipe_session_client PUBLIC coral::session_wire coral::product_identity)
target_link_libraries(coral_pipe_session_client PUBLIC Bcrypt)
target_include_directories(coral_pipe_session_client PRIVATE ${CMAKE_SOURCE_DIR})
coral_target_common(coral_pipe_session_client)

if(CMAKE_CL_64)
add_executable(coral_engine_host WIN32
    host_main.cpp
    session_endpoint.h
    engine_host.rc
    resource.h)
target_include_directories(coral_engine_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/modules/runtime
    ${CMAKE_SOURCE_DIR}/modules/foundation
    ${CMAKE_SOURCE_DIR}/modules/preferences
    ${CMAKE_SOURCE_DIR}/modules/phonetics
    ${CMAKE_SOURCE_DIR}/src/common)
target_compile_definitions(coral_engine_host PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
target_link_libraries(coral_engine_host PRIVATE
    coral_pipe_session_client
    coral::diagnostics
    coral_engine_core
    Wtsapi32
    Advapi32
    Userenv)
target_link_libraries(coral_engine_host PRIVATE Bcrypt)
coral_target_common(coral_engine_host)
coral_product_output(coral_engine_host CORAL_ID_SESSION_HOST_EXE)
coral_install_runtime(coral_engine_host)
endif()
