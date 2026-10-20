add_library(coral_session_bridge STATIC session_bridge.cpp session_bridge.h)
add_library(coral::session_bridge ALIAS coral_session_bridge)
target_include_directories(coral_session_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
coral_target_common(coral_session_bridge)
