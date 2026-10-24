add_executable(tsf_state_test
    tsf_state_test.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/windows/coraltsf/src/keydecision.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/windows/coraltsf/src/candidatestatemodel.cpp)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/platform/windows/coraltsf/src/candidatestatemodel.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_include_directories(tsf_state_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/windows/coraltsf/src)
target_compile_definitions(tsf_state_test PRIVATE UNICODE _UNICODE)
coral_target_common(tsf_state_test)

add_executable(tsf_sink_contract_test tsf_sink_contract_test.cpp)
target_include_directories(tsf_sink_contract_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/windows/coraltsf/src)
target_compile_definitions(tsf_sink_contract_test PRIVATE UNICODE _UNICODE)
coral_target_common(tsf_sink_contract_test)

add_executable(tsf_host_smoke tsf_host_smoke.cpp)
target_compile_definitions(tsf_host_smoke PRIVATE UNICODE _UNICODE)
target_link_libraries(tsf_host_smoke PRIVATE ole32 uuid)
coral_target_common(tsf_host_smoke)

if(TARGET coraltsf64)
    # Compile the production controller with a fake dictionary and sink. Do not
    # initialize installed preferences, connect to the host or register the IME.
    set(_tsf_source ${CMAKE_SOURCE_DIR}/src/platform/windows/coraltsf)
    add_executable(translation_controller_test translation_controller_test.cpp
        ${_tsf_source}/src/candidatecontroller.cpp
        ${_tsf_source}/src/candidatestatemodel.cpp
        ${_tsf_source}/src/doublepinyinconverter.cpp)
    foreach(_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_LIBRARIES)
        get_target_property(_value coraltsf64 ${_property})
        set_property(TARGET translation_controller_test PROPERTY ${_property} "${_value}")
    endforeach()
    target_precompile_headers(translation_controller_test PRIVATE ${_tsf_source}/pch.h)
    add_dependencies(translation_controller_test coraltsf64)
    coral_target_common(translation_controller_test)
    add_test(NAME translation_controller COMMAND translation_controller_test)
    set_tests_properties(translation_controller PROPERTIES TIMEOUT 30)
endif()
