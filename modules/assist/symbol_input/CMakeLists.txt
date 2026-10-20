add_library(uumodeprocessor STATIC
    symbol_handler.cpp
)

target_include_directories(uumodeprocessor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../phonetics
        ${CMAKE_CURRENT_SOURCE_DIR}/../../foundation
        ${CMAKE_CURRENT_SOURCE_DIR}/../../preferences   # settings.h (Phase 5 Step 4)   # log.h (Phase 5 Step 3)

)

target_link_libraries(uumodeprocessor PRIVATE coral_vocabulary)
coral_target_common(uumodeprocessor)
