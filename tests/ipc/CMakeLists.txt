cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(coralipc_tests LANGUAGES CXX)
    enable_testing()
    set(CMAKE_CXX_STANDARD 17)
    include(../../cmake/coral_common.cmake)
    add_subdirectory(../../src/platform/windows/ipc coralipc)
endif()

add_executable(coralipc_test coralipc_test.cpp)
target_include_directories(coralipc_test PRIVATE
    ../../src/platform/windows/ipc/common
    ../../src/platform/windows/ipc/ipcclient/client)
target_compile_definitions(coralipc_test PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(coralipc_test PRIVATE coralipc)
coral_target_common(coralipc_test)
add_test(NAME coralipc COMMAND coralipc_test)
set_tests_properties(coralipc PROPERTIES TIMEOUT 60)
