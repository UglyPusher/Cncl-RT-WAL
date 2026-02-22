# ---------------------------------------------------------------------------
# tests/primitives — standalone executables, no external test framework.
# Each binary returns 0 on success, non-zero on failure.
#
# Headers (stam/, sys/, primitives/) are provided by core_exec → core_sys
# which exposes include/ as an interface include directory.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Helper: add one primitive test executable and register it with CTest.
#
# Usage:
#   add_primitive_test(NAME <target> SOURCE <file.cpp> [THREADS])
#
# THREADS — links Threads::Threads (required for multi-threaded stress tests).
# ---------------------------------------------------------------------------
function(add_primitive_test)
    cmake_parse_arguments(ARG "THREADS" "NAME;SOURCE" "" ${ARGN})

    add_executable(${ARG_NAME} ${ARG_SOURCE})

    target_link_libraries(${ARG_NAME} PRIVATE core_exec)

    target_compile_options(${ARG_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
            -O2
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4
            /O2
            /permissive-
        >
    )

    if(ARG_THREADS)
        find_package(Threads REQUIRED)
        target_link_libraries(${ARG_NAME} PRIVATE Threads::Threads)
    endif()

    add_test(
        NAME    primitives::${ARG_NAME}
        COMMAND ${ARG_NAME}
    )

    set_tests_properties(primitives::${ARG_NAME} PROPERTIES
        LABELS              "primitives"
        TIMEOUT             60
        FAIL_REGULAR_EXPRESSION "FAIL"
    )
endfunction()

# ---------------------------------------------------------------------------
# Test targets
# ---------------------------------------------------------------------------

# crc32_rt — pure compute, constexpr, no threads
add_primitive_test(
    NAME    test_crc32_rt
    SOURCE  crc32_rt_test.cpp
)

# dbl_buffer — sustained concurrent stress requires threads
add_primitive_test(
    NAME    test_dbl_buffer
    SOURCE  dbl_buffer_test.cpp
    THREADS
)

# mailbox2slot — SPSC claim-verify stress requires threads
add_primitive_test(
    NAME    test_mailbox2slot
    SOURCE  mailbox2slot_test.cpp
    THREADS
)

# spsc_ring — FIFO no-loss stress requires threads
add_primitive_test(
    NAME    test_spsc_ring
    SOURCE  spsc_ring_test.cpp
    THREADS
)
