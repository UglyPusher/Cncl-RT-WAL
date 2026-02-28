# --------------------------------------------------
# Testing setup
# --------------------------------------------------

enable_testing()

# --------------------------------------------------
# Common test target
# --------------------------------------------------

add_executable(stam_tests
    crc32_rt_test.cpp
    dbl_buffer_test.cpp
    mailbox2slot_test.cpp
    spsc_ring_test.cpp
    spmc_snapshot_test.cpp
    main.cpp
)

target_link_libraries(stam_tests
    PRIVATE
        stam_primitives
)

target_compile_features(stam_tests
    PRIVATE
        cxx_std_20
)


# --------------------------------------------------
# CTest registration
# --------------------------------------------------

add_test(
    NAME stam_primitives_tests
    COMMAND stam_tests
)