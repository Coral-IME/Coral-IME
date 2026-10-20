add_executable(translation_client_test translation_client_test.cpp)
target_include_directories(translation_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/translation ${CMAKE_SOURCE_DIR}/modules/preferences)
target_link_libraries(translation_client_test PRIVATE coral::translation coral_preferences)
coral_target_common(translation_client_test)
add_test(NAME translation_client COMMAND translation_client_test)
add_test(NAME translation_python_protocol COMMAND ${Python3_EXECUTABLE} -m unittest discover
    -s ${CMAKE_CURRENT_SOURCE_DIR} -p test_aliyun_translation_probe.py)
