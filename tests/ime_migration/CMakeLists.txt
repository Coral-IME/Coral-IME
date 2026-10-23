add_test(NAME ime_migration_tool
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_tool.py)

add_executable(ime_migration_handoff_test handoff_test.cpp
    ../../apps/settings_app/other_ime_import_coordinator.cpp)
target_include_directories(ime_migration_handoff_test PRIVATE ../../apps/settings_app)
target_compile_features(ime_migration_handoff_test PRIVATE cxx_std_17)
target_compile_definitions(ime_migration_handoff_test PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(ime_migration_handoff_test PRIVATE user32 advapi32)
if(MSVC)
    target_compile_options(ime_migration_handoff_test PRIVATE /utf-8)
endif()
# 测试助手放在专用目录，绝不覆盖产品输出中的迁移脚本。
set_target_properties(ime_migration_handoff_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/handoff-bin")
add_test(NAME ime_migration_handoff COMMAND ime_migration_handoff_test)
