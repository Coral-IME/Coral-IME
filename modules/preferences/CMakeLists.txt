# 配置、共享设置与调用计时的公共实现库。
add_library(coral_preferences STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ini_store.cpp
    ${CMAKE_SOURCE_DIR}/platform/windows/preferences/credential_protection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mode_selection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mode_selection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/preference_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared_preferences.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/call_timing.cpp)

# PUBLIC include：使用方需要 ini_store.h / shared_preferences.h / call_timing.h 等头
target_include_directories(coral_preferences PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
# 日志只在实现内部使用。
target_link_libraries(coral_preferences PRIVATE coral_trace Crypt32)
target_link_libraries(coral_preferences PUBLIC coral::product_identity)
# shared_preferences.cpp 使用 boost/interprocess（vendored 于 src/lib）
# ini_store.h 经 "thirdparty/simpleini/SimpleIni.h" 依赖 common/thirdparty（平名根为 common/）
target_include_directories(coral_preferences PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/lib)

coral_target_common(coral_preferences)
