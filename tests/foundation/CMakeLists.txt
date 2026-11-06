add_executable(foundation_contract_test foundation_contract_test.cpp)
target_compile_features(foundation_contract_test PRIVATE cxx_std_17)
target_link_libraries(foundation_contract_test PRIVATE coral_foundation)
target_include_directories(foundation_contract_test PRIVATE ../../modules/preferences)
coral_target_common(foundation_contract_test)
add_test(NAME foundation_contract COMMAND foundation_contract_test)

add_executable(background_tasks_test background_tasks_test.cpp)
target_link_libraries(background_tasks_test PRIVATE coral_foundation)
coral_target_common(background_tasks_test)
add_test(NAME background_tasks COMMAND background_tasks_test)
set_tests_properties(background_tasks PROPERTIES TIMEOUT 30)

if(MSVC AND CMAKE_CL_64)
    add_executable(msvc_mutex_runtime_probe msvc_mutex_runtime_probe.cpp)
    coral_target_common(msvc_mutex_runtime_probe)
    add_test(NAME msvc_mutex_runtime COMMAND msvc_mutex_runtime_probe)
    set_tests_properties(msvc_mutex_runtime PROPERTIES TIMEOUT 15)

    if(MSVC_VERSION GREATER_EQUAL 1940)
        # 新工具链下保留一个未启用兼容构造的对照，仅用于隔离的旧运行库检查。
        add_executable(msvc_mutex_runtime_incompatible_control EXCLUDE_FROM_ALL
            msvc_mutex_runtime_probe.cpp)
        coral_target_common(msvc_mutex_runtime_incompatible_control)
        target_compile_definitions(msvc_mutex_runtime_incompatible_control PRIVATE
            CORAL_MUTEX_INCOMPATIBLE_CONTROL)
        set_target_properties(msvc_mutex_runtime_incompatible_control PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/incompatible-control/$<CONFIG>")
    endif()
endif()
