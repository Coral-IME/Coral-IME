# coral-pinyin 公共构建模块
#
# 关键缓存变量：
#   FUNCTIMER_OPEN   : 函数计时宏开关
#
# 第三方组件从候选内源码构建；诊断使用 Windows SDK 声明并动态加载系统 DbgHelp。
#
# 编码标准：整个项目统一 UTF-8。coral_target_common 对每个目标加 /utf-8（MSVC）；
# 特殊工程（ueh/opencc）亦单独补齐。资源文件 .rc 保持 UTF-16LE（MSVC 资源编译标准，非 GBK）。

# 统一输出目录：${CMAKE_BINARY_DIR}/coralpinyin；多配置构建追加 Debug/Release。
set(CORAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coralpinyin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CORAL_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CORAL_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 所有配置（含 Release）都生成 PDB，便于 WER/dump 崩溃符号定位。
# /Zi 编译期生成程序数据库，/DEBUG 让 DLL/EXE 链接出最终 <target>.pdb。
if(MSVC)
    add_compile_options(/Zi)
    add_link_options(/DEBUG)
endif()

# 所有目标统一放顶层 IDE 文件夹，避免平铺
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_library(coral_msvc_defs INTERFACE)
target_compile_definitions(coral_msvc_defs INTERFACE
    NOMINMAX
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
    _SCL_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
    _SECURE_SCL=0
    PSAPI_VERSION=1
    _ATL_XP_TARGETING
)

function(coral_target_common tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /Zc:__cplusplus /utf-8 /MP)
        set_property(TARGET ${tgt} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    target_link_libraries(${tgt} PRIVATE coral_msvc_defs)
    set_target_properties(${tgt} PROPERTIES FOLDER "coralpinyin")
endfunction()

function(coral_install_runtime tgt)
    install(TARGETS ${tgt} RUNTIME DESTINATION coralpinyin LIBRARY DESTINATION coralpinyin)
endfunction()


function(coral_product_output target identity_key)
    string(REGEX REPLACE "\\.(exe|dll)$" "" output_name "${${identity_key}}")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
endfunction()
