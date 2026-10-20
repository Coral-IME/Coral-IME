option(CORAL_ENABLE_UPDATES "Enable HTTPS update checks and installer downloads" ON)
set(CORAL_UPDATE_SERVICE_BASE_URL "https://coralime.cn" CACHE STRING "HTTPS update service base")
set(CORAL_EFFECTIVE_UPDATE_SERVICE_BASE_URL "")
if(CORAL_ENABLE_UPDATES)
    # 旧构建树缓存过空地址；启用更新时迁移到官网，不再只依赖某个预设。
    if(NOT CORAL_UPDATE_SERVICE_BASE_URL)
        set(CORAL_UPDATE_SERVICE_BASE_URL "https://coralime.cn" CACHE STRING "HTTPS update service base" FORCE)
    endif()
    if(NOT CORAL_UPDATE_SERVICE_BASE_URL MATCHES "^https://[A-Za-z0-9.-]+(:[0-9]+)?(/[A-Za-z0-9_./~-]+)?$")
        message(FATAL_ERROR "CORAL_UPDATE_SERVICE_BASE_URL must be an HTTPS host/base path without credentials, query, fragment or unsafe characters")
    endif()
    string(REGEX REPLACE "/+$" "" CORAL_EFFECTIVE_UPDATE_SERVICE_BASE_URL "${CORAL_UPDATE_SERVICE_BASE_URL}")
endif()
# 测试注册与实际生成头使用同一个有效开关，关闭时不会注册在线入口。
set(CORAL_UPDATES_CONFIGURED "${CORAL_ENABLE_UPDATES}" CACHE INTERNAL "Whether update requests are configured" FORCE)
configure_file(${CMAKE_CURRENT_LIST_DIR}/update_service_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/update_service_config.h @ONLY)
