add_library(coral_translation STATIC ${CMAKE_SOURCE_DIR}/platform/windows/translation/aliyun_client.cpp)
add_library(coral::translation ALIAS coral_translation)
target_include_directories(coral_translation PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/lib/opencc/deps/rapidjson-1.1.0)
target_link_libraries(coral_translation PRIVATE Bcrypt libcurl_static)
coral_target_common(coral_translation)
