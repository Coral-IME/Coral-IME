
add_library(coralipc STATIC
    # client
    ipcclient/client/ipcclient.cpp
    ipcclient/client/pipeclient.cpp
    # server
    ipcserver/server/ipcserver.cpp
    ipcserver/server/pipeserver.cpp
    # common
    common/data.cpp
    common/securityattribute.cpp
    common/pipe_peer.cpp
)

target_include_directories(coralipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(coralipc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/ipcclient
    ${CMAKE_CURRENT_SOURCE_DIR}/ipcserver
    ${CMAKE_CURRENT_SOURCE_DIR}/ipcclient/client
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../modules/foundation  # safec/
)
target_compile_definitions(coralipc PRIVATE _UNICODE UNICODE)
target_link_libraries(coralipc PUBLIC Userenv Advapi32)

coral_target_common(coralipc)
