#include <cstdio>
#include "./sys_preempt_lock.hpp"  // desktop no-op; replace with platform impl


void crc32_tests();
void dbl_buffer_tests();
void dbl_buffer_seqlock_tests();
void mailbox2slot_tests();
void mailbox2slot_smp_tests();
void spsc_ring_tests();
int  spmc_snapshot_tests();
void spmc_snapshot_smp_tests();

int main()
{
    printf("=== STAM primitives tests ===\n");

    crc32_tests();
    dbl_buffer_tests();
    dbl_buffer_seqlock_tests();
    mailbox2slot_tests();
    mailbox2slot_smp_tests();
    spsc_ring_tests();
    spmc_snapshot_tests();
    spmc_snapshot_smp_tests();

    printf("=== ALL TESTS PASSED ===\n");

    return 0;
}
