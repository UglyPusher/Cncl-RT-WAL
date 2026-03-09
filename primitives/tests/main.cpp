#include <cstdio>
#include "./sys_preempt_lock.hpp"  // desktop no-op; replace with platform impl
#include "test_filter.hpp"


int crc32_tests();
int dbl_buffer_tests();
int dbl_buffer_seqlock_tests();
int mailbox2slot_tests();
int mailbox2slot_smp_tests();
int spsc_ring_tests();
int spmc_snapshot_tests();
int spmc_snapshot_smp_tests();

int main(int argc, char** argv)
{
    stam::tests::configure_from_cli(argc, argv);
    printf("=== STAM primitives tests ===\n");

    if (stam::tests::should_run_suite("crc32")) {
        crc32_tests();
    }
    if (stam::tests::should_run_suite("dbl_buffer")) {
        dbl_buffer_tests();
    }
    if (stam::tests::should_run_suite("dbl_buffer_seqlock")) {
        dbl_buffer_seqlock_tests();
    }
    if (stam::tests::should_run_suite("mailbox2slot")) {
        mailbox2slot_tests();
    }
    if (stam::tests::should_run_suite("mailbox2slot_smp")) {
        mailbox2slot_smp_tests();
    }
    if (stam::tests::should_run_suite("spsc_ring")) {
        spsc_ring_tests();
    }
    if (stam::tests::should_run_suite("spmc_snapshot")) {
        spmc_snapshot_tests();
    }
    if (stam::tests::should_run_suite("spmc_snapshot_smp")) {
        spmc_snapshot_smp_tests();
    }

    printf("=== ALL TESTS PASSED ===\n");

    return 0;
}
