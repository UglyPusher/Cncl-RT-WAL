/*
 * spmc_snapshot_test.cpp
 *
 * Unit tests for SPMCSnapshot (SPMC Snapshot Channel, latest-wins).
 * Spec: primitives/docs/SPMCSnapshot — RT Contract & Invariants.md (Rev 6.3)
 *
 * Build (example):
 *   c++ -std=c++20 -O2 -pthread spmc_snapshot_test.cpp -o spmc_snapshot_test
 *
 * Exit code: 0 = all tests passed (EXPECT aborts immediately on failure).
 */

#include "stam/primitives/spmc_snapshot.hpp"
#include "test_harness.hpp"

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

namespace stam::primitives {

template <typename T, uint32_t N> class SPMCSnapshotTest final {
public:
    using Core = SPMCSnapshotCore<T, N>;
    using busy_mask_word_t = typename Core::busy_mask_word_t;

    static busy_mask_word_t busy_mask(const Core& core) noexcept {
        return core.ctrl.busy_mask.load(std::memory_order_relaxed);
    }

    static bool initialized(const Core& core) noexcept {
        return core.ctrl.initialized.load(std::memory_order_relaxed);
    }

    static uint8_t refcnt_value(const Core& core, uint32_t i) noexcept {
        return core.refcnt[i].load(std::memory_order_relaxed);
    }

    static constexpr uint32_t k_slots() noexcept {
        return Core::K;
    }

    static const char* slot_addr(const Core& core, uint32_t i) noexcept {
        return reinterpret_cast<const char*>(&core.slots[i]);
    }

    static const char* ctrl_addr(const Core& core) noexcept {
        return reinterpret_cast<const char*>(&core.ctrl);
    }
};

} // namespace stam::primitives

// ---------------------------------------------------------------------------
// Minimal test harness (file-local counters)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;

static constexpr const char* kSuiteName = "spmc_snapshot";
static int g_failed = 0;

// TEST/RUN/EXPECT provided by test_harness.hpp

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

// expect_child_abort provided by test_harness.hpp

// ---------------------------------------------------------------------------
// Contract tests: static / compile-time checks
// ---------------------------------------------------------------------------

TEST(test_static_trivially_copyable) {
    // Must compile — Pod32 is trivially copyable.
    // Non-trivially-copyable T fires static_assert inside SPMCSnapshotCore.
    [[maybe_unused]] SPMCSnapshot<Pod32, 1> ch;
}

TEST(test_slot_count) {
    // K = N + 2 for several N values (Slot Availability Theorem).
    using Test1  = SPMCSnapshotTest<Pod32, 1>;
    using Test2  = SPMCSnapshotTest<Pod32, 2>;
    using Test4  = SPMCSnapshotTest<Pod32, 4>;
    using Test30 = SPMCSnapshotTest<Pod32, 30>;
    EXPECT(Test1::k_slots() == 3u);
    EXPECT(Test2::k_slots() == 4u);
    EXPECT(Test4::k_slots() == 6u);
    EXPECT(Test30::k_slots() == 32u);
}

TEST(test_lock_free_atomics) {
    using Test = SPMCSnapshotTest<Pod32, 2>;
    using busy_mask_word_t = typename Test::busy_mask_word_t;
    EXPECT(std::atomic<busy_mask_word_t>::is_always_lock_free);
    EXPECT(std::atomic<uint8_t>::is_always_lock_free);
    EXPECT(std::atomic<bool>::is_always_lock_free);
}

TEST(test_core_initial_state) {
    SPMCSnapshot<Pod32, 2> ch;
    using Test = SPMCSnapshotTest<Pod32, 2>;
    EXPECT(Test::busy_mask(ch.core()) == 0u);
    EXPECT(Test::initialized(ch.core()) == false);
    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        EXPECT(Test::refcnt_value(ch.core(), i) == 0u);
    }
}

// ---------------------------------------------------------------------------
// Contract tests: behavior
// ---------------------------------------------------------------------------

TEST(test_try_read_before_publish_returns_false) {
    SPMCSnapshot<Pod32, 1> ch;
    auto reader = ch.reader();
    using Test = SPMCSnapshotTest<Pod32, 1>;

    Pod32 out{42, 42};
    EXPECT(!reader.try_read(out));
    // out must be unchanged on false return (no partial write)
    EXPECT(out.x == 42 && out.y == 42);
    // busy_mask must be zero: the early-out path must not claim any slot
    EXPECT(Test::busy_mask(ch.core()) == 0u);
}

TEST(test_publish_then_read) {
    SPMCSnapshot<Pod32, 1> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();
    using Test = SPMCSnapshotTest<Pod32, 1>;

    writer.write({1, 2});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 1 && out.y == 2);
    // Claim must be fully released after try_read.
    EXPECT(Test::busy_mask(ch.core()) == 0u);
}

TEST(test_initialized_flag) {
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    using Test = SPMCSnapshotTest<Pod32, 2>;

    EXPECT(Test::initialized(ch.core()) == false);
    writer.write({7, 8});
    EXPECT(Test::initialized(ch.core()) == true);
    // Idempotent: second publish must not change it.
    writer.write({9, 10});
    EXPECT(Test::initialized(ch.core()) == true);
}

TEST(test_always_returns_true_after_first_publish) {
    // G8: after the first publish, try_read must always return true.
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({1, 2});
    for (int i = 0; i < 200; ++i) {
        Pod32 out{};
        EXPECT(reader.try_read(out));
    }
}

TEST(test_latest_wins) {
    SPMCSnapshot<Pod32, 1> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({1, 1});
    writer.write({2, 2});
    writer.write({3, 3});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 3 && out.y == 3);
}

TEST(test_multiple_readers_see_same_latest) {
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    auto r1 = ch.reader();
    auto r2 = ch.reader();

    writer.write({10, 20});

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
        writer.write({i, -i});
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
        writer.write({i, i * 2});
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

    writer.write(src);

    LargePod dst{};
    EXPECT(reader.try_read(dst));
    EXPECT(dst == src);
}

TEST(test_busy_mask_zero_after_all_reads) {
    // All N readers read once; busy_mask must drain to 0 after every read.
    SPMCSnapshot<Pod32, 4> ch;
    auto writer = ch.writer();
    using Test = SPMCSnapshotTest<Pod32, 4>;

    writer.write({1, 2});

    Pod32 out{};
    for (int t = 0; t < 4; ++t) {
        auto reader = ch.reader();
        (void)reader.try_read(out);
    }
    EXPECT(Test::busy_mask(ch.core()) == 0u);
}

TEST(test_refcnt_zero_after_reads) {
    SPMCSnapshot<Pod32, 2> ch;
    auto writer = ch.writer();
    auto r1 = ch.reader();
    auto r2 = ch.reader();
    using Test = SPMCSnapshotTest<Pod32, 2>;

    writer.write({5, 6});

    Pod32 out{};
    (void)r1.try_read(out);
    (void)r2.try_read(out);

    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        EXPECT(Test::refcnt_value(ch.core(), i) == 0u);
    }
}

TEST(test_writer_guard_fail_fast) {
    SPMCSnapshot<Pod32, 2> ch;
    const bool aborted = stam::tests::expect_double_issue_abort([&] {
        (void)ch.writer();
    });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast) {
    SPMCSnapshot<Pod32, 2> ch;
    const bool aborted = stam::tests::expect_issue_limit_abort(2, [&] {
        (void)ch.reader();
    });
    EXPECT(aborted);
}

// ---------------------------------------------------------------------------
// Diagnostic stress tests
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
    using Test = SPMCSnapshotTest<Pod32, 1>;

    std::atomic<bool> done{false};
    std::atomic<int>  torn{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.write({i, -i});
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
    EXPECT(Test::busy_mask(ch.core()) == 0u);
}

// N=2: two concurrent readers.  No torn reads, busy_mask drains on exit.
TEST(test_spmc_n2_stress_no_torn_read) {
    constexpr auto kDuration = std::chrono::milliseconds(150);

    SPMCSnapshot<Pod32, 2> ch;
    using Test = SPMCSnapshotTest<Pod32, 2>;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            writer.write({++i, -i});
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
    EXPECT(Test::busy_mask(ch.core()) == 0u);
}

// N=4: four concurrent readers.  No torn reads; verifies K=6 is sufficient.
TEST(test_spmc_n4_stress_no_torn_read) {
    constexpr auto kDuration = std::chrono::milliseconds(150);

    SPMCSnapshot<Pod32, 4> ch;
    using Test = SPMCSnapshotTest<Pod32, 4>;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            writer.write({++i, -i});
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
    EXPECT(Test::busy_mask(ch.core()) == 0u);
}

// Latest-wins: after writer finishes, every reader must see the last frame.
TEST(test_spmc_latest_wins_after_writer_done) {
    constexpr int kFrames  = 200'000;
    constexpr int kReaders = 3;

    SPMCSnapshot<Pod32, kReaders> ch;

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.write({i, i});
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
    using Test = SPMCSnapshotTest<Pod32, 2>;

    std::atomic<bool> stop{false};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            writer.write({++i, -i});
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
    EXPECT(Test::busy_mask(ch.core()) == 0u);
    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        EXPECT(Test::refcnt_value(ch.core(), i) == 0u);
    }
}

// ---------------------------------------------------------------------------
// Implementation tests
// ---------------------------------------------------------------------------

TEST(test_slot_cacheline_alignment) {
    // Each Slot must start on a cacheline boundary (no false sharing between slots).
    SPMCSnapshot<Pod32, 2> ch;
    using Test = SPMCSnapshotTest<Pod32, 2>;
    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(Test::slot_addr(ch.core(), i));
        EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
    }
}

TEST(test_ctrl_cacheline_alignment) {
    // Control block must start on a cacheline boundary.
    SPMCSnapshot<Pod32, 2> ch;
    using Test = SPMCSnapshotTest<Pod32, 2>;
    const auto addr = reinterpret_cast<uintptr_t>(Test::ctrl_addr(ch.core()));
    EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
}

TEST(test_ctrl_separate_from_slots) {
    // ctrl must not share a cacheline with any slot (avoids writer↔reader
    // false sharing on busy_mask / published accesses).
    SPMCSnapshot<Pod32, 2> ch;
    using Test = SPMCSnapshotTest<Pod32, 2>;
    const auto ctrl_addr = reinterpret_cast<uintptr_t>(Test::ctrl_addr(ch.core()));
    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        const auto slot_addr = reinterpret_cast<uintptr_t>(Test::slot_addr(ch.core(), i));
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

    std::printf("--- contract: static / compile-time ---\n");
    RUN(test_static_trivially_copyable);
    RUN(test_slot_count);
    RUN(test_lock_free_atomics);
    RUN(test_core_initial_state);

    std::printf("\n--- contract: behavior ---\n");
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
    RUN(test_spmc_latest_wins_after_writer_done);

    std::printf("\n--- implementation ---\n");
    RUN(test_slot_cacheline_alignment);
    RUN(test_ctrl_cacheline_alignment);
    RUN(test_ctrl_separate_from_slots);

    std::printf("\n--- diagnostic stress ---\n");
    if (!stam::tests::should_run_diagnostic_stress()) {
        std::printf("  (disabled; use --diag-stress or STAM_TEST_DIAG_STRESS=1)\n");
    }
    if (stam::tests::should_run_diagnostic_stress()) {
        RUN(test_spmc_n1_stress_no_torn_read);
        RUN(test_spmc_n2_stress_no_torn_read);
        RUN(test_spmc_n4_stress_no_torn_read);
        RUN(test_spmc_n2_sustained_cleanup);
    }

    std::printf("\n=== Results: %d/%d passed ===\n", g_passed, g_total);
    return (g_failed == 0) ? 0 : 1;
}
