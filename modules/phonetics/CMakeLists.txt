# coral_engine_core：进程内静态词典核心

set(CORAL_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common)

set(CORAL_PHONETICS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/text_file_helpers.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/trace_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/recursive_lock.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/recursive_lock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/system_mutex_lease.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/legacy_stream_cipher.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/utf8conv.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../preferences/scalar_text_codec.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../preferences/ini_store.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../preferences/preference_registry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../preferences/shared_preferences.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../preferences/call_timing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/async_log_sink.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/coral/vocabulary/lexicon_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/safec/cstring_win.h

    ../runtime/core_operations.h
    ../runtime/core_operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/runtime_context.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/runtime_options.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/runtime_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/runtime_snapshot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/runtime_snapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/coral/composition/session_port.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/local_session_port.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_snapshot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_snapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_codec.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_codec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_storage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../learning/interaction_history_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_snapshot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_snapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_codec.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_codec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_storage.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ranking/candidate_rules_manager.cpp
    script/script_conversion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../composition/composition_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/candidate_source_bridge.cpp
    ../../include/coral/composition/composition_types.h
    ../../include/coral/composition/composition_session.h
    directed_lattice.hpp
    lattice_payload.hpp
    lexicon_catalog.h
    lexicon_catalog.cpp
    candidate_lattice.hpp
    syllable_helpers.h
    context_scorer.cpp
    context_scorer.h
    ../storage/bigram_table.h
    ../storage/bigram_table.cpp
    ../storage/lexicon_table.cpp
    syllable_parser.cpp
    syllable_parser.h
    phonetic_sequence.hpp
    syllable_lattice.cpp
    syllable_lattice.h
    candidate_decoder.h
    candidate_decoder.cpp
    shortest_paths.hpp
    phonetic_types.h
    lexicon_source.h
    personal_lexicon.h
    personal_lexicon.cpp
    lexicon_importer.h
    lexicon_importer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/coral/vocabulary/lexicon_import.h
    system_lexicon.h
    system_lexicon.cpp
    candidate_transforms.h
    candidate_transforms.cpp
    latin_lexicon.h
    source_csv.h
    latin_lexicon.cpp
    calendar_text.h
    calendar_text.cpp
    candidate_result.h
    candidate_result.cpp
    spelling_repair.h
    spelling_repair.cpp
    character_prior.h
    character_prior.cpp
    pronunciation_labeler.h
    pronunciation_labeler.cpp
    syllable_catalog.h
    support/object_pool.hpp
    ../storage/phrase_archive_view.hpp
    support/parse_arena.cpp
    support/parse_arena.hpp
    support/indexed_string_set.hpp
    ../foundation/slice_view.h
)

function(coral_configure_phonetics target_name)
    target_include_directories(${target_name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/support
            ${CMAKE_CURRENT_SOURCE_DIR}/../storage
            ${CMAKE_CURRENT_SOURCE_DIR}/../../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/../learning
        PRIVATE
            ${CORAL_COMMON_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src
            ${CMAKE_CURRENT_SOURCE_DIR}/../assist/shape_input
            ${CMAKE_CURRENT_SOURCE_DIR}/../assist/value_input
            ${CMAKE_CURRENT_SOURCE_DIR}/../assist/symbol_input
            ${CMAKE_CURRENT_SOURCE_DIR}/script
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/lib/marisa-trie/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/lib/opencc/deps/rapidjson-1.1.0
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/platform/windows/ipc/include
    )
    target_compile_definitions(${target_name} PRIVATE OS_WINDOW Opencc_BUILT_AS_STATIC)
    if(FUNCTIMER_OPEN)
        target_compile_definitions(${target_name} PRIVATE FUNCTIMER_OPEN)
    endif()
    target_precompile_headers(${target_name} PRIVATE pch.h)
    target_link_libraries(${target_name} PRIVATE
        opencc marisa-trie
        umodeprocessor vmodeprocessor uumodeprocessor
        coral_trace
        coral_preferences
        coral_foundation
        coral_resource_io
        coral_vocabulary
        coral_language_model
    )
    coral_target_common(${target_name})
endfunction()

# In-process engine core; external sessions use CSW4, never a C++ DLL ABI.
add_library(coral_engine_core STATIC ${CORAL_PHONETICS_SOURCES} pch.cpp)
add_library(coral::engine_core ALIAS coral_engine_core)
add_library(coral::phonetics ALIAS coral_engine_core)
coral_configure_phonetics(coral_engine_core)
option(CORAL_BUILD_MIXED_LANGUAGE_TEST "Build mixed Chinese/English engine regression" ON)

# ---- 资源与配置文件 ----
add_custom_command(TARGET coral_engine_core POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DCONFIG_DIR=${CORAL_CONFIG_DIR}" "-DOUTPUT_DIR=${CORAL_OUTPUT_DIR}"
        "-DRUNTIME_DIR=${CORAL_RUNTIME_DATA_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_resources.cmake"
    VERBATIM)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/engine_defaults.ini
    ${CORAL_OUTPUT_DIR}/config.ini COPYONLY)
