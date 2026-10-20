# coral_bundle_decoder: 安装期解包工具（纯 Win32 + XZ Embedded 静态链接，无 Python 依赖）
# 用法: coral_bundle_decoder <bundle.dictpack.lzma2> <coral_lexicon.csv> <coral_languagemodel.csv>

set(XZ_EMBEDDED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/lib/xz-embedded)

add_executable(coral_bundle_decoder
    main.cpp
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_crc32.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_crc64.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_dec_stream.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_dec_lzma2.c
    ${XZ_EMBEDDED_DIR}/linux/lib/xz/xz_dec_bcj.c
)

target_include_directories(coral_bundle_decoder PRIVATE
    ${XZ_EMBEDDED_DIR}/linux/include/linux
    ${XZ_EMBEDDED_DIR}/userspace
)

# XZ_USE_CRC64: Python lzma.compress 默认 CRC64 校验，必须启用
# XZ_DEC_ANY_CHECK: 接受未知校验类型（容错）
target_compile_definitions(coral_bundle_decoder PRIVATE
    XZ_USE_CRC64
    XZ_DEC_ANY_CHECK
)

coral_target_common(coral_bundle_decoder)
