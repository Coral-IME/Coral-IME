add_library(marisa-trie STATIC
    lib/marisa/agent.cc
    lib/marisa/keyset.cc
    lib/marisa/trie.cc
    lib/marisa/grimoire/io/mapper.cc
    lib/marisa/grimoire/io/reader.cc
    lib/marisa/grimoire/io/writer.cc
    lib/marisa/grimoire/trie/louds-trie.cc
    lib/marisa/grimoire/trie/tail.cc
    lib/marisa/grimoire/vector/bit-vector.cc
)

target_include_directories(marisa-trie
    PUBLIC include
    PRIVATE lib)
target_compile_definitions(marisa-trie PRIVATE _WIN32)

coral_target_common(marisa-trie)
