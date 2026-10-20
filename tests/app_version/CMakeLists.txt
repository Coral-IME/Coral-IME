cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(app_version_tests LANGUAGES CXX)
    enable_testing()
endif()

add_executable(app_version_test app_version_test.cpp)
target_compile_features(app_version_test PRIVATE cxx_std_17)
target_include_directories(app_version_test PRIVATE
    ../../modules/preferences
    ../../src/common)
if(MSVC)
    target_compile_options(app_version_test PRIVATE /utf-8)
    target_compile_definitions(app_version_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME app_version COMMAND app_version_test)
