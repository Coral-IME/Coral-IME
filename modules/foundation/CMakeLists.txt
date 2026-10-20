# 日志适配实现；使用方通过 coral_trace target 获取接口和依赖。
add_library(coral_trace STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/trace_log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async_log_sink.cpp)

target_include_directories(coral_trace PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
# 日志头不暴露 spdlog 类型，后端依赖保持 PRIVATE。
target_link_libraries(coral_trace PRIVATE spdlog::spdlog)
target_link_libraries(coral_trace PUBLIC coral::product_identity)

coral_target_common(coral_trace)
