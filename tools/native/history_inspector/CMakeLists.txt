add_executable(coral_history_inspector main.cpp)

target_include_directories(coral_history_inspector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/phonetics
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/foundation     # log.h (Phase 5 Step 3)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/preferences   # settings.h (Phase 5 Step 4)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/common         # thirdparty/SimpleIni (settings.h)
)
target_compile_definitions(coral_history_inspector PRIVATE OS_WINDOW)
target_link_libraries(coral_history_inspector PRIVATE coral::product_identity)

coral_target_common(coral_history_inspector)
