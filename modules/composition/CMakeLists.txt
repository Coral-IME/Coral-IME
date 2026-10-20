add_library(coral_composition STATIC composition_session.cpp)
add_library(coral::composition ALIAS coral_composition)
target_link_libraries(coral_composition PUBLIC coral::vocabulary)
coral_target_common(coral_composition)
