enable_testing()

add_executable(stam_exec_tests
    taskwrapper_test.cpp
    task_registry_test.cpp
    main.cpp
)

target_link_libraries(stam_exec_tests
    PRIVATE
        stam_exec
)

target_compile_features(stam_exec_tests
    PRIVATE
        cxx_std_20
)

add_test(
    NAME stam_exec_tests
    COMMAND stam_exec_tests
)
