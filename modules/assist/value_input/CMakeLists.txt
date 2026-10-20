add_library(vmodeprocessor STATIC
    value_handler.cpp
    date_processor.cpp
    time_processor.cpp
    arithmetic/arithmetic_processor.cpp
    arithmetic/expression_evaluator.cpp
    arithmetic/decimal_value.cpp
    number_text.cpp
)

target_include_directories(vmodeprocessor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../phonetics
        ${CMAKE_CURRENT_SOURCE_DIR}/../../foundation
        ${CMAKE_CURRENT_SOURCE_DIR}/../../preferences   # settings.h (Phase 5 Step 4)   # log.h (Phase 5 Step 3)

)

target_link_libraries(vmodeprocessor PRIVATE coral_vocabulary)
coral_target_common(vmodeprocessor)
