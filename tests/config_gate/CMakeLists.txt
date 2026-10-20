# Phase 6 Step 4 配置门闸测试 target
# 验证跨进程配置更新的行为语义：共享内存写入 → 下一词解析生效（代际门闸放行）、
# 无写时输出稳定（门闸关闭）。测试进程内的第二个 SharedMemoryConfig 实例
# 与引擎 DLL 共享同一命名段，等价于设置界面进程的写入视角。
add_executable(config_gate_test config_gate_test.cpp)

target_include_directories(config_gate_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/phonetics      # api.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common          # information.h 等被 api.h 引用
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/foundation     # log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/preferences   # sharedmemoryconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/phonetics  # help.h / api.h
)

target_link_libraries(config_gate_test PRIVATE coral_engine_core coral_preferences)

coral_target_common(config_gate_test)
