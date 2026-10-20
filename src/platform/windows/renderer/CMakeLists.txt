add_library(coral_renderer_protocol STATIC
    renderer_protocol.cpp
    renderer_protocol.h
    remote_renderer_port.cpp
    remote_renderer_port.h)
target_include_directories(coral_renderer_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(coral_renderer_protocol PUBLIC coral_vocabulary coral::product_identity Advapi32)
coral_target_common(coral_renderer_protocol)

add_library(coral_candidate_view STATIC
    candidate_view.cpp
    candidate_view.h
    candidate_theme.cpp
    candidate_theme.h
    soft_canvas.h)
target_include_directories(coral_candidate_view PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(coral_candidate_view PUBLIC User32 Gdi32 Advapi32 Ole32 Windowscodecs)
coral_target_common(coral_candidate_view)

add_library(coral_candidate_backdrop STATIC candidate_backdrop.cpp candidate_backdrop.h)
target_include_directories(coral_candidate_backdrop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(coral_candidate_backdrop PUBLIC User32 Gdi32 Advapi32 Dwmapi)
coral_target_common(coral_candidate_backdrop)

# x86/x64 TSF clients share the fixed-width protocol. The product renderer is
# a single native x64 process per user session.
if(CMAKE_CL_64)
    add_executable(coral_renderer WIN32 main.cpp renderer.rc resource.h)
    target_include_directories(coral_renderer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/platform/windows/speech
        ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(coral_renderer PRIVATE OS_WINDOW _UNICODE UNICODE)
    target_link_libraries(coral_renderer PRIVATE
        coral_renderer_protocol coral_candidate_view coral_candidate_backdrop coral_preferences coral::foundation User32 Gdi32 Advapi32 Userenv)
    coral_target_common(coral_renderer)
coral_product_output(coral_renderer CORAL_ID_RENDERER_EXE)
    coral_install_runtime(coral_renderer)

    # Built-in candidate backgrounds, resolved from <install>/res/skins at runtime.
    add_custom_command(TARGET coral_renderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CORAL_OUTPUT_DIR}/res/skins")
    foreach(image candidate_bg_1.jpg candidate_bg_2.jpg candidate_bg_3.jpg)
        add_custom_command(TARGET coral_renderer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/skins/${image}"
                "${CORAL_OUTPUT_DIR}/res/skins/${image}")
    endforeach()
endif()
