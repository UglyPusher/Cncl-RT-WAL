#include <cstdio>
#include "./sys_preempt_lock.hpp"  // desktop no-op; replace with platform impl


void crc32_tests();
void dbl_buffer_tests();
void mailbox2slot_tests();
void spsc_ring_tests();
int  spmc_snapshot_tests();

int main()
{
    printf("=== STAM primitives tests ===\n");

    crc32_tests();
    dbl_buffer_tests();
    mailbox2slot_tests();
    spsc_ring_tests();
    spmc_snapshot_tests();

    printf("=== ALL TESTS PASSED ===\n");

    return 0;
}