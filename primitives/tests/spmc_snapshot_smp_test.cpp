/*
 * spmc_snapshot_smp_test.cpp
 *
 * Stress tests for SPMCSnapshotSmp (SPMC Snapshot Channel, SMP-safe).
 * Spec: primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md (Rev 1.1)
 */

#include "stam/primitives/spmc_snapshot_smp.hpp"
#include "test_harness.hpp"
#include "stam/primitives/snapshot_concepts.hpp"
#include "stam/sys/sys_align.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

using namespace stam::primitives;

namespace stam::primitives {

template <typename T, uint32_t N> class SPMCSnapshotSmpTest final {
public:
    using Core = SPMCSnapshotSmpCore<T, N>;
    using busy_mask_word_t = typename Core::busy_mask_word_t;

    static busy_mask_word_t busy_mask(const Core& core) noexcept {
        return core.ctrl.busy_mask.load(std::memory_order_relaxed);
    }

    static uint8_t refcnt_value(const Core& core, uint32_t i) noexcept {
        return core.refcnt[i].load(std::memory_order_relaxed);
    }

    static constexpr uint32_t k_slots() noexcept {
        return Core::K;
    }
};

} // namespace stam::primitives

static int g_total  = 0;
static int g_passed = 0;

static constexpr const char* kSuiteName = "spmc_snapshot_smp";
static int g_failed = 0;

// TEST/RUN/EXPECT provided by test_harness.hpp

struct Pod32 {
    int32_t x{0};
    int32_t y{0};
};

// expect_child_abort provided by test_harness.hpp

TEST(test_concepts) {
    static_assert(SnapshotWriter<SPMCSnapshotSmpWriter<Pod32, 2>, Pod32>,
                  "SPMCSnapshotSmpWriter must satisfy SnapshotWriter");
    static_assert(SnapshotReader<SPMCSnapshotSmpReader<Pod32, 2>, Pod32>,
                  "SPMCSnapshotSmpReader must satisfy SnapshotReader");
}

TEST(test_lock_free_atomics) {
    using busy_mask_word_t = SPMCSnapshotSmpCore<Pod32, 2>::busy_mask_word_t;
    EXPECT(std::atomic<busy_mask_word_t>::is_always_lock_free);
    EXPECT(std::atomic<uint8_t>::is_always_lock_free);
    EXPECT(std::atomic<bool>::is_always_lock_free);
}

TEST(test_try_read_before_publish_returns_false) {
    SPMCSnapshotSmp<Pod32, 2> ch;
    auto r = ch.reader();
    Pod32 out{1, 1};
    EXPECT(!r.try_read(out));
    EXPECT(out.x == 1 && out.y == 1);
}

TEST(test_write_alias_and_publish_visible) {
    SPMCSnapshotSmp<Pod32, 2> ch;
    auto w = ch.writer();
    auto r = ch.reader();

    w.write({5, -5});
    Pod32 out{};
    EXPECT(r.try_read(out));
    EXPECT(out.x == 5 && out.y == -5);
}

TEST(test_refcnt_and_busy_mask_cleanup) {
    SPMCSnapshotSmp<Pod32, 2> ch;
    auto w = ch.writer();
    auto r0 = ch.reader();
    auto r1 = ch.reader();
    using Test = SPMCSnapshotSmpTest<Pod32, 2>;

    w.write({10, -10});
    Pod32 a{}, b{};
    EXPECT(r0.try_read(a));
    EXPECT(r1.try_read(b));

    EXPECT(Test::busy_mask(ch.core()) == 0u);
    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        EXPECT(Test::refcnt_value(ch.core(), i) == 0u);
    }
}

TEST(test_writer_guard_fail_fast) {
    SPMCSnapshotSmp<Pod32, 2> ch;
    const bool aborted = stam::tests::expect_double_issue_abort([&] {
        (void)ch.writer();
    });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast) {
    SPMCSnapshotSmp<Pod32, 2> ch;
    const bool aborted = stam::tests::expect_issue_limit_abort(2, [&] {
        (void)ch.reader();
    });
    EXPECT(aborted);
}

TEST(test_stress_n1_no_torn_read) {
    constexpr int kFrames = 200'000;
    SPMCSnapshotSmp<Pod32, 1> ch;

    std::atomic<bool> done{false};
    std::atomic<int> torn{0};

    std::thread tw([&] {
        auto w = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            w.write({i, -i});
        }
        done.store(true, std::memory_order_release);
    });

    std::thread tr([&] {
        auto r = ch.reader();
        Pod32 out{};
        while (!done.load(std::memory_order_acquire) || out.x != kFrames) {
            if (r.try_read(out) && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    tw.join();
    tr.join();
    EXPECT(torn.load() == 0);
}

TEST(test_stress_n2_no_torn_read) {
    constexpr int kFrames = 150'000;
    SPMCSnapshotSmp<Pod32, 2> ch;

    std::atomic<bool> done{false};
    std::atomic<int> torn{0};

    std::thread tw([&] {
        auto w = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            w.write({i, -i});
        }
        done.store(true, std::memory_order_release);
    });

    auto reader_job = [&] {
        auto r = ch.reader();
        Pod32 out{};
        while (!done.load(std::memory_order_acquire) || out.x != kFrames) {
            if (r.try_read(out) && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread tr0(reader_job);
    std::thread tr1(reader_job);

    tw.join();
    tr0.join();
    tr1.join();
    EXPECT(torn.load() == 0);
}

TEST(test_stress_sustained_cleanup) {
    constexpr auto kDuration = std::chrono::milliseconds(200);
    SPMCSnapshotSmp<Pod32, 4> ch;
    using Test = SPMCSnapshotSmpTest<Pod32, 4>;

    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::atomic<int> reads{0};

    std::thread tw([&] {
        auto w = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            w.write({i, -i});
        }
    });

    auto reader_job = [&] {
        auto r = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (r.try_read(out)) {
                reads.fetch_add(1, std::memory_order_relaxed);
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    };

    std::thread tr0(reader_job);
    std::thread tr1(reader_job);
    std::thread tr2(reader_job);
    std::thread tr3(reader_job);

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    tw.join();
    tr0.join();
    tr1.join();
    tr2.join();
    tr3.join();

    const int torn_count = torn.load();
    const int read_count = reads.load();
    const double torn_per_read = (read_count > 0)
        ? static_cast<double>(torn_count) / static_cast<double>(read_count)
        : 0.0;
    std::printf("    torn/read: %d/%d (%.6f)\n", torn_count, read_count, torn_per_read);
    EXPECT(read_count > 0);
    EXPECT(torn_count == 0);
    EXPECT(Test::busy_mask(ch.core()) == 0u);
    for (uint32_t i = 0; i < Test::k_slots(); ++i) {
        EXPECT(Test::refcnt_value(ch.core(), i) == 0u);
    }
}

// Single-shot SMP behavior diagnostic:
// after initialization, try_read() may return false when publication changes
// between claim and re-verify. We report miss/read ratio without hard bound.
TEST(test_stress_single_shot_miss_rate) {
    constexpr auto kDuration = std::chrono::milliseconds(200);
    SPMCSnapshotSmp<Pod32, 2> ch;

    std::atomic<bool> stop{false};
    std::atomic<int> misses{0};
    std::atomic<int> reads{0};

    std::thread tw([&] {
        auto w = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            w.write({i, -i});
        }
    });

    auto reader_job = [&] {
        auto r = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (r.try_read(out)) {
                reads.fetch_add(1, std::memory_order_relaxed);
            } else {
                misses.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread tr0(reader_job);
    std::thread tr1(reader_job);

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    tw.join();
    tr0.join();
    tr1.join();

    const int miss_count = misses.load();
    const int read_count = reads.load();
    const int attempts = miss_count + read_count;
    const double miss_per_attempt = (attempts > 0)
        ? static_cast<double>(miss_count) / static_cast<double>(attempts)
        : 0.0;
    std::printf("    miss/read: %d/%d (miss/attempt=%.6f)\n",
                miss_count, read_count, miss_per_attempt);
    EXPECT(attempts > 0);
    EXPECT(miss_count >= 0 && miss_count <= attempts);
}

int spmc_snapshot_smp_tests() {
    std::printf("=== SPMCSnapshotSmp tests ===\n\n");

    std::printf("--- functional ---\n");
    RUN(test_concepts);
    RUN(test_lock_free_atomics);
    RUN(test_try_read_before_publish_returns_false);
    RUN(test_write_alias_and_publish_visible);
    RUN(test_refcnt_and_busy_mask_cleanup);
    RUN(test_writer_guard_fail_fast);
    RUN(test_reader_guard_fail_fast);

    std::printf("\n--- multi-threaded stress ---\n");
    RUN(test_stress_n1_no_torn_read);
    RUN(test_stress_n2_no_torn_read);
    RUN(test_stress_sustained_cleanup);
    RUN(test_stress_single_shot_miss_rate);

    std::printf("\n  passed: %d / %d\n\n", g_passed, g_total);
    return 0;
}
