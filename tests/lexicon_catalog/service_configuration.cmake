include("${CMAKE_CURRENT_LIST_DIR}/../../modules/lexicon_catalog/validate_service_base.cmake")
coral_validate_lexicon_service_base("${INPUT_BASE}" normalized)
file(WRITE "${OUTPUT_FILE}" "${normalized}")
