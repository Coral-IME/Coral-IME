# 安装端无损还原工具；沿用已有XZ Embedded，不依赖Python。
set(XZ_EMBEDDED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/lib/xz-embedded)
add_executable(coral_table_restore main.cpp
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_crc32.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_crc64.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_dec_stream.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_dec_lzma2.c)
target_include_directories(coral_table_restore PRIVATE
    ${XZ_EMBEDDED_DIR}/linux/include/linux ${XZ_EMBEDDED_DIR}/userspace)
target_compile_definitions(coral_table_restore PRIVATE XZ_USE_CRC64)
target_link_libraries(coral_table_restore PRIVATE Bcrypt coral::language_model)
coral_target_common(coral_table_restore)
if(BUILD_TESTING)
    add_test(NAME compact_runtime_tables COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/release/test_compact_tables.py $<TARGET_FILE:coral_table_restore>)
    set_tests_properties(compact_runtime_tables PROPERTIES TIMEOUT 120)
endif()
