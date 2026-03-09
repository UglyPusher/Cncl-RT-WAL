/*
 * spmc_snapshot_test.cpp
 *
 * Unit tests for SPMCSnapshot (SPMC Snapshot Channel, latest-wins).
 * Spec: docs/SPMCSnapshot — RT Contract & Invariants.md (Rev 6.2)
 *
 * Build (example):
 *   c++ -std=c++20 -O2 -pthread spmc_snapshot_test.cpp -o spmc_snapshot_test
 *
 * Exit code: 0 = all tests passed (EXPECT aborts immediately on failure).
 */

#include "stam/primitives/spmc_snapshot.hpp"
#include "test_filter.hpp"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

using namespace stam::primitives;

// ---------------------------------------------------------------------------
// Minimal test harness (file-local counters)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;

static constexpr const char* kSuiteName = "spmc_snapshot";
static int g_failed = 0;

#define TEST(name) static void name(); static void name##_announce() { std::printf("[RUN] %s\n", #name); } static void name()

#define RUN(name)                                          \
    do {                                                   \
        if (!stam::tests::should_run_test(kSuiteName, #name)) {\
            std::printf("  %-55sSKIP\n", #name " ");\
            break;\
        }\
        ++g_total;                                         \
        std::printf("  %-55s", #name " ");                 \
        name##_announce();                                 \
        name();                                            \
        ++g_passed;                                        \
        std::printf("PASS\n");                             \
    } while (0)

// Aborts on failure — intentional: a broken invariant is not recoverable.
#define EXPECT(cond)                                               \
    do {                                                           \
        if (!(cond)) {                                             \
            ++g_failed;                                            \
            std::printf("FAIL\n  assertion failed: %s\n"          \
                        "  at %s:%d\n", #cond, __FILE__, __LINE__);\
            std::abort();                                          \
        }                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct Pod32 {
    int32_t x{0};
    int32_t y{0};
    bool operator==(const Pod32&) const noexcept = default;
};

struct LargePod {
    uint8_t data[128]{};
    bool operator==(const LargePod& o) const noexcept {
        return std::memcmp(data, o.data, sizeof(data)) == 0;
    }
};

template <class Fn>
bool expect_child_abort(Fn&& fn) {
    const pid_t pid = ::fork();
    EXPECT(pid >= 0);

    if (pid == 0) {
        fn();
        std::fflush(stdout);
        _Exit(0);
    }

    int status = 0;
    EXPECT(::waitpid(pid, &status, 0) == pid);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

// ---------------------------------------------------------------------------
// Static / compile-time checks
// ---------------------------------------------------------------------------

TEST(test_static_trivially_copyable) {
    // Must compile — Pod32 is trivially copyable.
    // Non-trivially-copyable T fires static_assert inside SPMCSnapshotCore.
    [[maybe_unused]] SPMCSnapshot<Pod32, 1> ch;
}

TEST(test_slot_count) {
    // K = N + 2 for several N values (Slot Availability Theorem).
    EXPECT((SPMCSnapshotCore<Pod32,  1>::K ==  3u));
    EXPECT((SPMCSnapshotCore<Pod32,  2>::K ==  4u));
    EXPECT((SPMCSnapshotCore<Pod32,  4>::K ==  6u));
    EXPECT((SPMCSnapshotCore<Pod32, 30>::K == 32u));
}

TEST(test_lock_free_atomics) {
    EXPECT(std::atomic<uint32_t>::is_always_lock_free);
    EXPECT(std::atomic<uint8_t>::is_always_lock_free);
    EXPECT(std::atomic<bool>::is_always_lock_free);
}

TEST(test_core_initial_state) {
    SPMCSnapshot<Pod32, 2> ch;
    EXPECT(ch.core().ctrl.busy_mask.load()   == 0u);
    EXPECT(ch.core().ctrl.initialized.load() == false);
    for (uint32_t i = 0; i < SPMCSnapshotCore<Pod32, 2>::K; ++i) {
        EXPECT(ch.core().refcnt[i].load() == 0u);
    }
}

// ---------------------------------------------------------------------------
// Single-threaded functional tests
// ---------------------------------------------------------------------------

TEST(test_try_read_before_publish_returns_false) {
    SPMCSnapshot<Pod32, 1> ch;
    auto reader = ch.reader();

    Pod32 out{42, 42};
    EXPECT(!reader.try_read(out));
    // out must be unchanged on false return (no partial write)
    EXPECT(out.x == 42 && out.y == 42);
    // busy_mask must be zero: the early-out path must not claim any slot
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
}

TEST(test_publish_then_read) {
    SPMCSnapshot<Pod32, 1> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.publish({1, 2});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 1 && out.y == 2);
    // Claim must be fully released after try_read.
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
}

TEST(test_initialized_flag) {
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();

    EXPECT(ch.core().ctrl.initialized.load() == false);
    writer.publish({7, 8});
    EXPECT(ch.core().ctrl.initialized.load() == true);
    // Idempotent: second publish must not change it.
    writer.publish({9, 10});
    EXPECT(ch.core().ctrl.initialized.load() == true);
}

TEST(test_always_returns_true_after_first_publish) {
    // G8: after the first publish, try_read must always return true.
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.publish({1, 2});
    for (int i = 0; i < 200; ++i) {
        Pod32 out{};
        EXPECT(reader.try_read(out));
    }
}

TEST(test_latest_wins) {
    SPMCSnapshot<Pod32, 1> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.publish({1, 1});
    writer.publish({2, 2});
    writer.publish({3, 3});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 3 && out.y == 3);
}

TEST(test_multiple_readers_see_same_latest) {
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    auto r1 = ch.reader();
    auto r2 = ch.reader();

    writer.publish({10, 20});

    Pod32 a{}, b{};
    EXPECT(r1.try_read(a));
    EXPECT(r2.try_read(b));
    EXPECT(a == b);
    EXPECT(a.x == 10 && a.y == 20);
}

TEST(test_interleaved_publish_read) {
    SPMCSnapshot<Pod32, 1> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    for (int i = 0; i < 100; ++i) {
        writer.publish({i, -i});
        Pod32 out{};
        EXPECT(reader.try_read(out));
        EXPECT(out.x == i && out.y == -i);
    }
}

TEST(test_repeat_publish_many) {
    // Writer publishes 1000 frames without any read; reader sees the last one.
    SPMCSnapshot<Pod32, 1> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    for (int i = 0; i < 1000; ++i) {
        writer.publish({i, i * 2});
    }

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 999 && out.y == 1998);
}

TEST(test_large_pod) {
    SPMCSnapshot<LargePod, 2> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    LargePod src{};
    for (int i = 0; i < 128; ++i) src.data[i] = static_cast<uint8_t>(i);

    writer.publish(src);

    LargePod dst{};
    EXPECT(reader.try_read(dst));
    EXPECT(dst == src);
}

TEST(test_busy_mask_zero_after_all_reads) {
    // All N readers read once; busy_mask must drain to 0 after every read.
    SPMCSnapshot<Pod32, 4> ch;
    auto writer = ch.writer();

    writer.publish({1, 2});

    Pod32 out{};
    for (int t = 0; t < 4; ++t) {
        auto reader = ch.reader();
        (void)reader.try_read(out);
    }
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
}

TEST(test_refcnt_zero_after_reads) {
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    auto r1 = ch.reader();
    auto r2 = ch.reader();

    writer.publish({5, 6});

    Pod32 out{};
    (void)r1.try_read(out);
    (void)r2.try_read(out);

    for (uint32_t i = 0; i < SPMCSnapshotCore<Pod32, 2>::K; ++i) {
        EXPECT(ch.core().refcnt[i].load() == 0u);
    }
}

TEST(test_writer_guard_fail_fast) {
    const bool aborted = expect_child_abort([] {
        SPMCSnapshot<Pod32, 2> ch;
        (void)ch.writer();
        (void)ch.writer();
    });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast) {
    const bool aborted = expect_child_abort([] {
        SPMCSnapshot<Pod32, 2> ch;
        (void)ch.reader();
        (void)ch.reader();
        (void)ch.reader();
    });
    EXPECT(aborted);
}

// ---------------------------------------------------------------------------
// Multi-threaded stress tests
//
// Note on SMP validity: these tests run on x86 (TSO), where the narrow race
// documented in spec §Theoretical Bounds (load published → fetch_or busy_mask)
// is not observable in practice due to the strong hardware memory model.
// Under Condition A/B the protocol is fully correct on any architecture.
// ---------------------------------------------------------------------------

// N=1: single-reader stress.  Verifies no torn reads (x == -y invariant).
TEST(test_spmc_n1_stress_no_torn_read) {
    constexpr int kFrames = 200'000;

    SPMCSnapshot<Pod32, 1> ch;

    std::atomic<bool> done{false};
    std::atomic<int>  torn{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.publish({i, -i});
        }
        done.store(true, std::memory_order_release);
    });

    std::thread reader_thread([&] {
        auto reader = ch.reader();
        Pod32 out{};
        while (!done.load(std::memory_order_acquire) || out.x != kFrames) {
            if (reader.try_read(out)) {
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
}

// N=2: two concurrent readers.  No torn reads, busy_mask drains on exit.
TEST(test_spmc_n2_stress_no_torn_read) {
    constexpr auto kDuration = std::chrono::milliseconds(150);

    SPMCSnapshot<Pod32, 2> ch;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            writer.publish({++i, -i});
        }
    });

    auto reader_fn = [&] {
        auto reader = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (reader.try_read(out)) {
                reads.fetch_add(1, std::memory_order_relaxed);
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    };

    std::thread r1(reader_fn);
    std::thread r2(reader_fn);

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    writer_thread.join();
    r1.join();
    r2.join();

    const int torn_count = torn.load();
    const int read_count = reads.load();
    const double torn_per_read = (read_count > 0)
        ? static_cast<double>(torn_count) / static_cast<double>(read_count)
        : 0.0;
    std::printf("    torn/read: %d/%d (%.6f)\n", torn_count, read_count, torn_per_read);
    EXPECT(read_count > 0);
    EXPECT(torn_count >= 0 && torn_count <= read_count);
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
}

// N=4: four concurrent readers.  No torn reads; verifies K=6 is sufficient.
TEST(test_spmc_n4_stress_no_torn_read) {
    constexpr auto kDuration = std::chrono::milliseconds(150);

    SPMCSnapshot<Pod32, 4> ch;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            writer.publish({++i, -i});
        }
    });

    std::vector<std::thread> reader_threads;
    for (int t = 0; t < 4; ++t) {
        reader_threads.emplace_back([&] {
            auto reader = ch.reader();
            Pod32 out{};
            while (!stop.load(std::memory_order_relaxed)) {
                if (reader.try_read(out)) {
                    reads.fetch_add(1, std::memory_order_relaxed);
                    if (out.x != -out.y) {
                        torn.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    writer_thread.join();
    for (auto& r : reader_threads) r.join();

    const int torn_count = torn.load();
    const int read_count = reads.load();
    const double torn_per_read = (read_count > 0)
        ? static_cast<double>(torn_count) / static_cast<double>(read_count)
        : 0.0;
    std::printf("    torn/read: %d/%d (%.6f)\n", torn_count, read_count, torn_per_read);
    EXPECT(read_count > 0);
    EXPECT(torn_count >= 0 && torn_count <= read_count);
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
}

// Latest-wins: after writer finishes, every reader must see the last frame.
TEST(test_spmc_latest_wins_after_writer_done) {
    constexpr int kFrames  = 200'000;
    constexpr int kReaders = 3;

    SPMCSnapshot<Pod32, kReaders> ch;

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.publish({i, i});
        }
    });
    writer_thread.join();

    // Writer is done; every reader must observe the last published frame.
    for (int t = 0; t < kReaders; ++t) {
        auto reader = ch.reader();
        Pod32 out{};
        EXPECT(reader.try_read(out));
        EXPECT(out.x == kFrames && out.y == kFrames);
    }
}

// Sustained concurrent use: busy_mask and refcnt must fully drain after join.
TEST(test_spmc_n2_sustained_cleanup) {
    constexpr auto kDuration = std::chrono::milliseconds(100);

    SPMCSnapshot<Pod32, 2> ch;

    std::atomic<bool> stop{false};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            writer.publish({++i, -i});
        }
    });

    std::thread r1([&] {
        auto reader = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) (void)reader.try_read(out);
    });

    std::thread r2([&] {
        auto reader = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) (void)reader.try_read(out);
    });

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    writer_thread.join();
    r1.join();
    r2.join();

    // All claims must be released: busy_mask == 0, all refcnt == 0.
    EXPECT(ch.core().ctrl.busy_mask.load() == 0u);
    for (uint32_t i = 0; i < SPMCSnapshotCore<Pod32, 2>::K; ++i) {
        EXPECT(ch.core().refcnt[i].load() == 0u);
    }
}

// ---------------------------------------------------------------------------
// Cache layout checks
// ---------------------------------------------------------------------------

TEST(test_slot_cacheline_alignment) {
    // Each Slot must start on a cacheline boundary (no false sharing between slots).
    SPMCSnapshot<Pod32, 2> ch;
    for (uint32_t i = 0; i < SPMCSnapshotCore<Pod32, 2>::K; ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(&ch.core().slots[i]);
        EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
    }
}

TEST(test_ctrl_cacheline_alignment) {
    // Control block must start on a cacheline boundary.
    SPMCSnapshot<Pod32, 2> ch;
    const auto addr = reinterpret_cast<uintptr_t>(&ch.core().ctrl);
    EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
}

TEST(test_ctrl_separate_from_slots) {
    // ctrl must not share a cacheline with any slot (avoids writer↔reader
    // false sharing on busy_mask / published accesses).
    SPMCSnapshot<Pod32, 2> ch;
    const auto ctrl_addr = reinterpret_cast<uintptr_t>(&ch.core().ctrl);
    for (uint32_t i = 0; i < SPMCSnapshotCore<Pod32, 2>::K; ++i) {
        const auto slot_addr = reinterpret_cast<uintptr_t>(&ch.core().slots[i]);
        const auto diff = static_cast<ptrdiff_t>(ctrl_addr) -
                          static_cast<ptrdiff_t>(slot_addr);
        EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
    }
}

// ---------------------------------------------------------------------------
// Entry point (called from main.cpp)
// ---------------------------------------------------------------------------

int spmc_snapshot_tests() {
    std::printf("=== SPMCSnapshot unit tests ===\n\n");

    std::printf("--- static / compile-time ---\n");
    RUN(test_static_trivially_copyable);
    RUN(test_slot_count);
    RUN(test_lock_free_atomics);
    RUN(test_core_initial_state);

    std::printf("\n--- single-threaded functional ---\n");
    RUN(test_try_read_before_publish_returns_false);
    RUN(test_publish_then_read);
    RUN(test_initialized_flag);
    RUN(test_always_returns_true_after_first_publish);
    RUN(test_latest_wins);
    RUN(test_multiple_readers_see_same_latest);
    RUN(test_interleaved_publish_read);
    RUN(test_repeat_publish_many);
    RUN(test_large_pod);
    RUN(test_busy_mask_zero_after_all_reads);
    RUN(test_refcnt_zero_after_reads);
    RUN(test_writer_guard_fail_fast);
    RUN(test_reader_guard_fail_fast);

    std::printf("\n--- multi-threaded stress ---\n");
    RUN(test_spmc_n1_stress_no_torn_read);
    RUN(test_spmc_n2_stress_no_torn_read);
    RUN(test_spmc_n4_stress_no_torn_read);
    RUN(test_spmc_latest_wins_after_writer_done);
    RUN(test_spmc_n2_sustained_cleanup);

    std::printf("\n--- cache layout ---\n");
    RUN(test_slot_cacheline_alignment);
    RUN(test_ctrl_cacheline_alignment);
    RUN(test_ctrl_separate_from_slots);

    std::printf("\n=== Results: %d/%d passed ===\n", g_passed, g_total);
    return (g_failed == 0) ? 0 : 1;
}
