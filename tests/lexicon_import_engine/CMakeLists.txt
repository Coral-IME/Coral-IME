# 用户词库导入：在真实主词典上验证自动注音、查重、写入与撤销（需要引擎数据目录）。
add_executable(lexicon_import_engine_test lexicon_import_engine_test.cpp)

target_include_directories(lexicon_import_engine_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/phonetics
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/foundation
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/preferences
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/phonetics
)

target_link_libraries(lexicon_import_engine_test PRIVATE coral_engine_core)
target_link_libraries(lexicon_import_engine_test PRIVATE coral::lexicon_io)

coral_target_common(lexicon_import_engine_test)
