# Phase 4 按键序列测试 target：无 COM、无窗口驱动 coral::composition::Session
# State and dictionary adapters are linked through the static engine core.
add_executable(sequence_test
    sequence_test.cpp)

target_include_directories(sequence_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include            # coral/ime/*
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/phonetics         # api.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common             # help.h 等
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/foundation     # log.h (Phase 5 Step 3)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/preferences   # settings.h (Phase 5 Step 4)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/phonetics     # defindes.h 等
)

target_compile_definitions(sequence_test PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
target_link_libraries(sequence_test PRIVATE coral_engine_core)

coral_target_common(sequence_test)
