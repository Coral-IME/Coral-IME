add_library(coral_lexicon_io STATIC text_import.cpp)
add_library(coral::lexicon_io ALIAS coral_lexicon_io)
target_link_libraries(coral_lexicon_io PUBLIC coral::vocabulary)
coral_target_common(coral_lexicon_io)
