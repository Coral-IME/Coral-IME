# usersharedmemoryread：用户共享内存读取工具

add_executable(usersharedmemoryread
    src/main.cpp
    src/structure.h
)

target_include_directories(usersharedmemoryread PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib
)
target_compile_definitions(usersharedmemoryread PRIVATE OS_WINDOW)

coral_target_common(usersharedmemoryread)
