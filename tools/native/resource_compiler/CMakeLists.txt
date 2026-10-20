add_executable(coral_resource_compiler main.cpp)
target_include_directories(coral_resource_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/foundation ${CMAKE_SOURCE_DIR}/modules/preferences
    ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/lib)
target_link_libraries(coral_resource_compiler PRIVATE coral::engine_core)
coral_target_common(coral_resource_compiler)
