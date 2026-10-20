# coralnet：基于 libcurl 的轻量网络库（去 Qt 第 1 步）
# 纯 std 接口（std::string + std::function 回调），不依赖 Qt。
#
# libcurl 以源码子集形式内置（src/lib/curl，来自上游 curl 8.22.x），
# 静态链接，Windows 下使用 Schannel 做 TLS（不引入 OpenSSL）。
set(CURL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib/curl" CACHE INTERNAL "")

# libcurl 构建选项（FORCE 防被命令行意外覆盖）
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF) # Only disable curl tests in this directory scope.
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)   # Windows 原生 TLS
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_UNICODE ON CACHE BOOL "" FORCE)      # Win32 wide API
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

add_subdirectory(${CURL_ROOT_DIR} ${CMAKE_BINARY_DIR}/lib/curl-build EXCLUDE_FROM_ALL)

add_library(coralnet STATIC
    src/coralnet.cpp)

target_include_directories(coralnet PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# PRIVATE：libcurl 的编译选项/宏（CURL_STATICLIB 等）不外泄给使用方
target_link_libraries(coralnet PRIVATE libcurl_static)

coral_target_common(coralnet)
