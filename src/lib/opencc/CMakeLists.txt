set(OPENCC_SOURCES
    src/Config.cpp
    src/Conversion.cpp
    src/ConversionChain.cpp
    src/Converter.cpp
    src/Dict.cpp
    src/DictConverter.cpp
    src/DictEntry.cpp
    src/DictGroup.cpp
    src/Lexicon.cpp
    src/MarisaDict.cpp
    src/MaxMatchSegmentation.cpp
    src/PhraseExtract.cpp
    src/SerializedValues.cpp
    src/SimpleConverter.cpp
    src/Segmentation.cpp
    src/TextDict.cpp
    src/UTF8StringSlice.cpp
    src/UTF8Util.cpp
)

add_library(opencc STATIC ${OPENCC_SOURCES})

target_include_directories(opencc
    PUBLIC src
    PRIVATE
        ../marisa-trie/include
        deps/rapidjson-1.1.0
        deps/tclap-1.2.2
        deps/darts-clone
)
target_compile_definitions(opencc PRIVATE _WIN32 Opencc_BUILT_AS_STATIC)
target_link_libraries(opencc PRIVATE marisa-trie)

coral_target_common(opencc)
