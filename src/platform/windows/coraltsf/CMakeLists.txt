# coraltsf：Windows TSF 输入法服务（DLL；x64 下产物名 coraltsf64）

if(CMAKE_CL_64)
    set(PROJECT_TARGET_NAME coraltsf64)
else()
    set(PROJECT_TARGET_NAME coraltsf32)
endif()

# MIDL 生成 COM 接口代理文件
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ime_i.c ${CMAKE_CURRENT_BINARY_DIR}/ime.h
    COMMAND midl /h ${CMAKE_CURRENT_BINARY_DIR}/ime.h
        /iid ${CMAKE_CURRENT_BINARY_DIR}/ime_i.c
        /proxy ${CMAKE_CURRENT_BINARY_DIR}/ime_p.c
        /dlldata ${CMAKE_CURRENT_BINARY_DIR}/ime_d.c
        /tlb ${CMAKE_CURRENT_BINARY_DIR}/ime.tlb
        ${CMAKE_CURRENT_SOURCE_DIR}/ime.idl
    DEPENDS ime.idl
    VERBATIM)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/ime_i.c PROPERTIES GENERATED TRUE)
# rc 编译时从 binary dir 找 ime.tlb（TYPELIB 资源）
set_source_files_properties(ime.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/ime.tlb")

# 让 pch.h 的 #include "ime.h" / "ime_i.c" 找到生成文件（通过 binary dir include 已覆盖）

add_library(${PROJECT_TARGET_NAME} SHARED
    ime.def
    ime.rc
    menu_icons.rc
    register.cpp
    ime.idl
    resource.h
    res/contextmenu/hide_toolbar.ico
    res/contextmenu/customize_toolbar.ico
    res/contextmenu/settings.ico
    res/contextmenu/about.ico
    res/contextmenu/feedback.ico
    dllmain.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ime_i.c
    xdlldata.c
    src/coreapi.cpp
    src/tsfapi.cpp
    src/textservice.cpp
    src/csettingmenu.cpp
    src/compartment.cpp
    src/languageprofile.cpp
    src/keyeventsink.cpp
    src/keydecision.cpp
    src/candidatestatemodel.cpp
    src/candidatecontroller.cpp
    src/texteditsink.cpp
    src/textlayoutsink.cpp
    src/threadfocussink.cpp
    src/threadmgreventsink.cpp
    src/contextstore.cpp
    src/configauthority.cpp
    src/session_bridge_factory.cpp
    src/renderercoordinator.cpp
    src/inprocesscandidatewindow.cpp
    src/ownerthreaddispatcher.cpp
    src/tsfthreadcontext.cpp
    src/editsession.cpp
    src/processmonitor.cpp
    src/candidatelistuielement.cpp
    src/displayattribute.cpp
    src/doublepinyinconverter.cpp
    pch.cpp
)

target_include_directories(${PROJECT_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include         # coral/ime/*
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc/include
    ${CMAKE_SOURCE_DIR}/platform/windows/session_host
    ${CMAKE_CURRENT_SOURCE_DIR}/../renderer
)
target_compile_definitions(${PROJECT_TARGET_NAME} PRIVATE OS_WINDOW _UNICODE UNICODE)
if(FUNCTIMER_OPEN)
    target_compile_definitions(${PROJECT_TARGET_NAME} PRIVATE FUNCTIMER_OPEN)
endif()

target_link_libraries(${PROJECT_TARGET_NAME} PRIVATE
    coral_preferences
    coral::translation
    coral_trace
    Psapi
    coralipc
    coral_pipe_session_client
    coral::session_bridge
    coral::host_bootstrap
    coral_renderer_protocol
    coral_candidate_view
)


target_precompile_headers(${PROJECT_TARGET_NAME} PRIVATE pch.h)
# C 文件不能强制包含 C++ 的 pch.h（PCH），跳过预编译头
set_source_files_properties(xdlldata.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# MIDL 生成的 C 文件同样不能强制包含 C++ PCH
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/ime_i.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
coral_target_common(${PROJECT_TARGET_NAME})
if(CMAKE_CL_64)
    coral_product_output(${PROJECT_TARGET_NAME} CORAL_ID_TSF_X64_DLL)
else()
    coral_product_output(${PROJECT_TARGET_NAME} CORAL_ID_TSF_X86_DLL)
endif()

# x64 产品构建同时产出唯一的每用户 engine host。x86 TSF 复用该 x64 host，
# 因而 Win32 配置本身不重复生成一个 32 位引擎进程。
if(CMAKE_CL_64)
    add_dependencies(${PROJECT_TARGET_NAME} coral_engine_host coral_renderer)
endif()

# 注册资源与语言栏图标。
foreach(res
    ime.ini
    rgs/ime.rgs
    rgs/textservice.rgs
    rgs/langbaritembutton.rgs
    rgs/editsession.rgs
    res/coral.ico res/pinyin.ico res/zh.ico res/en.ico res/A.ico res/coral_logo.ico
)
    add_custom_command(TARGET ${PROJECT_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${res}"
            "${CORAL_OUTPUT_DIR}/${res}")
endforeach()
