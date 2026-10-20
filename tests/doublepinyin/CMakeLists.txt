cmake_minimum_required(VERSION 3.21)

# This parser test can also be built independently on non-Windows platforms.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(doublepinyin_metadata_tests LANGUAGES CXX)
    enable_testing()
endif()

add_executable(doublepinyin_metadata_test doublepinyin_metadata_test.cpp)
target_compile_features(doublepinyin_metadata_test PRIVATE cxx_std_17)
target_include_directories(doublepinyin_metadata_test PRIVATE
    ../../modules/preferences
    ../../src/common)
if(MSVC)
    target_compile_options(doublepinyin_metadata_test PRIVATE /utf-8)
    target_compile_definitions(doublepinyin_metadata_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME doublepinyin_metadata COMMAND doublepinyin_metadata_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/config/doublepinyin")
