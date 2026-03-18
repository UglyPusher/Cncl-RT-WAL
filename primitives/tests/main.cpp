#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include "./sys_preempt_lock.hpp"  // desktop no-op; replace with platform impl
#include "test_filter.hpp"


int crc32_tests();
int dbl_buffer_tests();
int dbl_buffer_seqlock_tests();
int mailbox2slot_tests();
int mailbox2slot_smp_tests();
int spsc_ring_tests();
int spsc_ring_drop_oldest_tests();
int spmc_snapshot_tests();
int spmc_snapshot_smp_tests();

static int run_suite(const char* name, int (*suite_fn)()) {
    if (!stam::tests::should_run_suite(name)) {
        return 0;
    }

    const pid_t pid = ::fork();
    if (pid == 0) {
        const int rc = suite_fn();
        std::fflush(stdout);
        std::fflush(stderr);
        _Exit(rc);
    }
    if (pid < 0) {
        std::perror("fork");
        return 1;
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        std::perror("waitpid");
        return 1;
    }

    if (WIFSIGNALED(status)) {
        std::printf("=== Suite %s aborted by signal %d ===\n",
                    name, WTERMSIG(status));
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        std::printf("=== Suite %s failed (exit %d) ===\n",
                    name, WEXITSTATUS(status));
        return 1;
    }

    return 0;
}

int main(int argc, char** argv) {
    stam::tests::configure_from_cli(argc, argv);
    printf("=== STAM primitives tests ===\n");

    int failures = 0;
    failures += run_suite("crc32", crc32_tests);
    failures += run_suite("dbl_buffer", dbl_buffer_tests);
    failures += run_suite("dbl_buffer_seqlock", dbl_buffer_seqlock_tests);
    failures += run_suite("mailbox2slot", mailbox2slot_tests);
    failures += run_suite("mailbox2slot_smp", mailbox2slot_smp_tests);
    failures += run_suite("spsc_ring", spsc_ring_tests);
    failures += run_suite("spsc_ring_drop_oldest", spsc_ring_drop_oldest_tests);
    failures += run_suite("spmc_snapshot", spmc_snapshot_tests);
    failures += run_suite("spmc_snapshot_smp", spmc_snapshot_smp_tests);

    if (failures == 0) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    }

    printf("=== TESTS FAILED: %d suite(s) ===\n", failures);
    return 1;
}
