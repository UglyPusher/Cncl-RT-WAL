add_executable(test_unit
    test_double_buffer_invariants.cpp
    test_spsc_ring_invariants.cpp
)

target_link_libraries(test_unit
    PRIVATE
        core_exec      # ВАЖНО: без rtr и modules
)

add_test(NAME unit COMMAND test_unit)
