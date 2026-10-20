add_library(coral_session_wire STATIC session_wire.cpp)
add_library(coral::session_wire ALIAS coral_session_wire)
target_link_libraries(coral_session_wire PUBLIC coral::vocabulary)
coral_target_common(coral_session_wire)
