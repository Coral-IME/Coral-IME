add_executable(coral_lexicon_builder
    main.cpp
    ../../../modules/storage/lexicon_table.cpp
    ../../../modules/storage/resource_table_builder.cpp
    ../../../modules/storage/bigram_table.cpp
)

target_include_directories(coral_lexicon_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/phonetics
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../modules/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/lib/marisa-trie/include
)
target_compile_definitions(coral_lexicon_builder PRIVATE OS_WINDOW)
target_link_libraries(coral_lexicon_builder PRIVATE marisa-trie)

coral_target_common(coral_lexicon_builder)
