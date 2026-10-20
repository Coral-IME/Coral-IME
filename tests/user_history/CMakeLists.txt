add_executable(user_history_codec_test
    user_history_codec_test.cpp
    ../../modules/learning/interaction_history_types.cpp
    ../../modules/learning/interaction_history_snapshot.cpp
    ../../modules/learning/interaction_history_codec.cpp
    ../../modules/learning/interaction_history_storage.cpp
    ../../modules/ranking/candidate_rules_types.cpp
    ../../modules/ranking/candidate_rules_snapshot.cpp
    ../../modules/ranking/candidate_rules_codec.cpp
    ../../modules/ranking/candidate_rules_storage.cpp
    ../../modules/ranking/candidate_rules_manager.cpp)

target_include_directories(user_history_codec_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/learning
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/ranking)

coral_target_common(user_history_codec_test)
