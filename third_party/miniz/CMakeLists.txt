add_library(coral_miniz STATIC miniz.c miniz_tdef.c miniz_tinfl.c miniz_zip.c)
target_include_directories(coral_miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(coral_miniz PRIVATE _CRT_SECURE_NO_WARNINGS)
