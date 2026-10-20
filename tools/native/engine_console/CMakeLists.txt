# coral_engine_console：引擎交互式调试控制台（脱离 TSF 直接驱动 coral_engine_core）
add_executable(coral_engine_console main.cpp)

target_include_directories(coral_engine_console PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/phonetics            # api.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/foundation    # log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/preferences  # settings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/phonetics            # defindes.h 等
)

target_link_libraries(coral_engine_console PRIVATE coral_engine_core)

coral_target_common(coral_engine_console)
