# --------------------------------------------------
# Testing setup
# --------------------------------------------------

enable_testing()

# --------------------------------------------------
# Common test target
# --------------------------------------------------

set(STAM_TEST_SOURCES
    crc32_rt_test.cpp
    dbl_buffer_test.cpp
    dbl_buffer_seqlock_test.cpp
    mailbox2slot_test.cpp
    mailbox2slot_smp_test.cpp
    spsc_ring_test.cpp
    spmc_snapshot_test.cpp
    spmc_snapshot_smp_test.cpp
)

add_executable(stam_tests
    ${STAM_TEST_SOURCES}
    sys_preempt_lock.cpp
    test_filter.cpp
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

function(add_stam_suite_test target_name source_file suite_fn)
    add_executable(${target_name}
        ${source_file}
        sys_preempt_lock.cpp
        test_filter.cpp
        suite_main.cpp
    )

    target_link_libraries(${target_name}
        PRIVATE
            stam_primitives
    )

    target_compile_features(${target_name}
        PRIVATE
            cxx_std_20
    )

    target_compile_definitions(${target_name}
        PRIVATE
            STAM_SUITE_FN=${suite_fn}
    )

    add_test(
        NAME ${target_name}
        COMMAND ${target_name}
    )
endfunction()

add_stam_suite_test(stam_crc32_tests              crc32_rt_test.cpp          crc32_tests)
add_stam_suite_test(stam_dbl_buffer_tests         dbl_buffer_test.cpp        dbl_buffer_tests)
add_stam_suite_test(stam_dbl_buffer_seqlock_tests dbl_buffer_seqlock_test.cpp dbl_buffer_seqlock_tests)
add_stam_suite_test(stam_mailbox2slot_tests       mailbox2slot_test.cpp      mailbox2slot_tests)
add_stam_suite_test(stam_mailbox2slot_smp_tests   mailbox2slot_smp_test.cpp  mailbox2slot_smp_tests)
add_stam_suite_test(stam_spsc_ring_tests          spsc_ring_test.cpp         spsc_ring_tests)
add_stam_suite_test(stam_spmc_snapshot_tests      spmc_snapshot_test.cpp     spmc_snapshot_tests)
add_stam_suite_test(stam_spmc_snapshot_smp_tests  spmc_snapshot_smp_test.cpp spmc_snapshot_smp_tests)
