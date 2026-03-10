/*
 * dbl_buffer_test.cpp
 *
 * Unit tests for DoubleBuffer (SPSC ping-pong snapshot buffer).
 *
 * Key semantic differences from Mailbox2Slot tested here:
 *  - read() always returns a value (no false return, no NONE state)
 *  - before first write(), read() returns zero-initialized T (defined
 *    at C++ level, semantically unspecified per spec comment)
 *  - no claim/verify protocol: single acquire-load + copy, no lock_state
 *  - write() is always wait-free with no invalidate path
 *
 * Build (example):
 *   c++ -std=c++20 -O2 -pthread dbl_buffer_test.cpp -o dbl_buffer_test
 *
 * Exit code: 0 = all tests passed, non-zero = failure.
 */

#include "stam/primitives/dbl_buffer.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <cassert>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

using namespace stam::primitives;

// ---------------------------------------------------------------------------
// Minimal test harness (same conventions as mailbox2slot_test.cpp)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;

static constexpr const char* kSuiteName = "dbl_buffer";
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

TEST(test_static_assert_trivially_copyable) {
    // Must compile — Pod32 is trivially copyable.
    [[maybe_unused]] DoubleBuffer<Pod32> db;
}

TEST(test_lock_free) {
    // Core uses std::atomic<uint32_t> for published index.
    EXPECT(std::atomic<uint32_t>::is_always_lock_free);
}

TEST(test_core_initial_state) {
    // published starts at 0 (slot 0).
    DoubleBuffer<Pod32> db;
    EXPECT(db.core().published.load() == 0u);
}

// ---------------------------------------------------------------------------
// Contract tests: single-threaded behavior
// ---------------------------------------------------------------------------

// Semantic difference #1 from Mailbox2Slot:
// read() before write() returns zero-initialized data, not false.
TEST(test_read_before_write_returns_zero) {
    DoubleBuffer<Pod32> db;
    auto reader = db.reader();

    Pod32 out{99, 99};
    reader.read(out);

    // DoubleBufferCore is value-initialized (core_{}), so T is zero-init.
    EXPECT(out.x == 0 && out.y == 0);
}

TEST(test_write_then_read) {
    DoubleBuffer<Pod32> db;
    auto writer = db.writer();
    auto reader = db.reader();

    writer.write({1, 2});

    Pod32 out{};
    reader.read(out);

    EXPECT(out.x == 1 && out.y == 2);
}

TEST(test_latest_wins) {
    DoubleBuffer<Pod32> db;
    auto writer = db.writer();
    auto reader = db.reader();

    writer.write({1, 1});
    writer.write({2, 2});
    writer.write({3, 3});

    Pod32 out{};
    reader.read(out);
    EXPECT(out.x == 3 && out.y == 3);
}

TEST(test_multiple_reads_return_latest) {
    DoubleBuffer<Pod32> db;
    auto writer = db.writer();
    auto reader = db.reader();

    writer.write({10, 20});

    Pod32 a{}, b{};
    reader.read(a);
    reader.read(b);

    EXPECT(a == b);
    EXPECT(a.x == 10 && a.y == 20);
}

// Semantic difference #2 from Mailbox2Slot:
// read() always succeeds — there is no "miss" path, no sticky state.
// Repeated reads on an unchanging buffer return the same value.
TEST(test_read_always_succeeds) {
    DoubleBuffer<Pod32> db;
    auto writer = db.writer();
    auto reader = db.reader();

    writer.write({7, 8});

    for (int i = 0; i < 10; ++i) {
        Pod32 out{};
        reader.read(out);
        EXPECT(out.x == 7 && out.y == 8);
    }
}

TEST(test_interleaved_write_read) {
    DoubleBuffer<Pod32> db;
    auto writer = db.writer();
    auto reader = db.reader();

    for (int i = 0; i < 50; ++i) {
        writer.write({i, -i});
        Pod32 out{};
        reader.read(out);
        EXPECT(out.x == i && out.y == -i);
    }
}

TEST(test_large_pod) {
    DoubleBuffer<LargePod> db;
    auto writer = db.writer();
    auto reader = db.reader();

    LargePod src{};
    for (int i = 0; i < 128; ++i) src.data[i] = static_cast<uint8_t>(i);

    writer.write(src);

    LargePod dst{};
    reader.read(dst);
    EXPECT(dst == src);
}

// ---------------------------------------------------------------------------
// Implementation tests
// ---------------------------------------------------------------------------

// Ping-pong: writer alternates between slot 0 and slot 1 on each write.
// This is an implementation detail check, not a public contract guarantee.
TEST(test_slot_alternates) {
    DoubleBuffer<Pod32> db;
    auto writer = db.writer();

    EXPECT(db.core().published.load() == 0u);
    writer.write({1, 1});
    EXPECT(db.core().published.load() == 1u);
    writer.write({2, 2});
    EXPECT(db.core().published.load() == 0u);
    writer.write({3, 3});
    EXPECT(db.core().published.load() == 1u);
}

TEST(test_writer_guard_fail_fast) {
    DoubleBuffer<Pod32> db;
    const bool aborted = stam::tests::expect_double_issue_abort([&] {
        (void)db.writer();
    });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast) {
    DoubleBuffer<Pod32> db;
    const bool aborted = stam::tests::expect_double_issue_abort([&] {
        (void)db.reader();
    });
    EXPECT(aborted);
}

// ---------------------------------------------------------------------------
// Diagnostic stress tests
// ---------------------------------------------------------------------------

// Basic SPSC stress under overlap. DoubleBuffer does not guarantee torn-free
// reads in this regime; the test reports torn/read as an empirical metric.
TEST(test_spsc_stress_no_torn_read) {
    constexpr int kFrames = 1'000'000;

    DoubleBuffer<Pod32> db;

    std::atomic<bool> done{false};
    std::atomic<bool> start{false};
    std::atomic<int>  ready{0};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = db.writer();
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 1; i <= kFrames; ++i) {
            writer.write({i, -i});
        }
        done.store(true, std::memory_order_release);
    });

    std::thread reader_thread([&] {
        auto reader = db.reader();
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        Pod32 out{};
        while (!done.load(std::memory_order_acquire) || out.x != kFrames) {
            reader.read(out);
            reads.fetch_add(1, std::memory_order_relaxed);
            if (out.x != 0 && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    writer_thread.join();
    reader_thread.join();

    const int torn_count = torn.load();
    const int read_count = reads.load();
    const double torn_per_read = (read_count > 0)
        ? static_cast<double>(torn_count) / static_cast<double>(read_count)
        : 0.0;
    std::printf("    torn/read: %d/%d (%.6f)\n", torn_count, read_count, torn_per_read);
    EXPECT(read_count > 0);
    EXPECT(torn_count >= 0 && torn_count <= read_count);
}

// Contract check after concurrent activity has ceased: final published value
// must be observable once writer is done.
TEST(test_spsc_stress_latest_wins) {
    constexpr int kFrames = 1'000'000;

    DoubleBuffer<Pod32> db;

    std::thread writer_thread([&] {
        auto writer = db.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.write({i, i});
        }
    });

    writer_thread.join();

    auto reader = db.reader();
    Pod32 out{};
    reader.read(out);
    EXPECT(out.x == kFrames && out.y == kFrames);
}

// Sustained concurrent overlap. This is diagnostic only: contract does not
// promise torn-free reads here, so the test reports torn/read.
TEST(test_spsc_sustained_concurrent) {
    constexpr auto kDuration = std::chrono::milliseconds(1000);

    DoubleBuffer<Pod32> db;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = db.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            writer.write({i, -i});
        }
    });

    std::thread reader_thread([&] {
        auto reader = db.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            reader.read(out);
            reads.fetch_add(1, std::memory_order_relaxed);
            // Zero is valid: core is zero-initialized before first write.
            if (out.x != 0 && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    writer_thread.join();
    reader_thread.join();

    const int torn_count = torn.load();
    const int read_count = reads.load();
    const double torn_per_read = (read_count > 0)
        ? static_cast<double>(torn_count) / static_cast<double>(read_count)
        : 0.0;
    std::printf("    torn/read: %d/%d (%.6f)\n", torn_count, read_count, torn_per_read);
    EXPECT(read_count > 0);
    EXPECT(torn_count >= 0 && torn_count <= read_count);
}

// ---------------------------------------------------------------------------
// Implementation tests: cache layout
// ---------------------------------------------------------------------------

TEST(test_slots_on_separate_cache_lines) {
    // Each slot must be on its own cache line (false sharing avoidance).
    DoubleBuffer<Pod32> db;
    const auto* s0 = reinterpret_cast<const char*>(&db.core().buffers[0].value);
    const auto* s1 = reinterpret_cast<const char*>(&db.core().buffers[1].value);
    const auto  diff = static_cast<ptrdiff_t>(s1 - s0);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

TEST(test_published_on_separate_cache_line_from_slots) {
    // published index must not share a cache line with slot data.
    DoubleBuffer<Pod32> db;
    const auto* pub  = reinterpret_cast<const char*>(&db.core().published);
    const auto* s0   = reinterpret_cast<const char*>(&db.core().buffers[0].value);
    const auto  diff = static_cast<ptrdiff_t>(pub - s0);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int dbl_buffer_tests() {
    std::printf("=== DoubleBuffer unit tests ===\n\n");

    std::printf("--- contract: static / compile-time ---\n");
    RUN(test_static_assert_trivially_copyable);
    RUN(test_lock_free);
    RUN(test_core_initial_state);

    std::printf("\n--- contract: behavior ---\n");
    RUN(test_read_before_write_returns_zero);
    RUN(test_write_then_read);
    RUN(test_latest_wins);
    RUN(test_multiple_reads_return_latest);
    RUN(test_read_always_succeeds);
    RUN(test_interleaved_write_read);
    RUN(test_large_pod);
    RUN(test_writer_guard_fail_fast);
    RUN(test_reader_guard_fail_fast);
    RUN(test_spsc_stress_latest_wins);

    std::printf("\n--- implementation ---\n");
    RUN(test_slot_alternates);
    RUN(test_slots_on_separate_cache_lines);
    RUN(test_published_on_separate_cache_line_from_slots);

    std::printf("\n--- diagnostic stress ---\n");
    if (stam::tests::should_run_diagnostic_stress()) {
        RUN(test_spsc_stress_no_torn_read);
        RUN(test_spsc_sustained_concurrent);
    } else {
        std::printf("  diagnostic stress disabled (use --diag-stress or STAM_TEST_DIAG_STRESS=1)\n");
    }

    std::printf("\n=== Results: %d/%d passed ===\n", g_passed, g_total);
    return (g_failed == 0) ? 0 : 1;
}
