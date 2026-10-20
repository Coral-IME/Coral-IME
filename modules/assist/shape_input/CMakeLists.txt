add_library(umodeprocessor STATIC
    shape_handler.cpp
    shape_lexicon.cpp
)

target_include_directories(umodeprocessor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../phonetics
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/lib/marisa-trie/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../foundation
        ${CMAKE_CURRENT_SOURCE_DIR}/../../preferences   # settings.h (Phase 5 Step 4)   # log.h (Phase 5 Step 3)
)
target_link_libraries(umodeprocessor PUBLIC coral_foundation PRIVATE marisa-trie coral_vocabulary)

coral_target_common(umodeprocessor)
