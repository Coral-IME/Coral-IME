add_library(coral_resource_io STATIC
    verified_resource.cpp
    ../../src/common/thirdparty/hash-library/md5.cpp)
add_library(coral::resource_io ALIAS coral_resource_io)
if(WIN32)
    target_sources(coral_resource_io PRIVATE ../../platform/windows/named_resource.cpp)
    target_link_libraries(coral_resource_io PRIVATE Advapi32)
else()
    target_sources(coral_resource_io PRIVATE ../../platform/posix/file_resource.cpp)
endif()
target_include_directories(coral_resource_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common)
target_compile_features(coral_resource_io PUBLIC cxx_std_17)
coral_target_common(coral_resource_io)
