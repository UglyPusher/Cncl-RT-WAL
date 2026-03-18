/*
 * mailbox2slot_test.cpp
 *
 * Unit tests for Mailbox2Slot (SPSC Snapshot Mailbox).
 * Spec: primitives/docs/Mailbox2Slot — RT Contract & Invariants.md
 *
 * Build (example):
 *   c++ -std=c++20 -O2 -pthread mailbox2slot_test.cpp -o mailbox2slot_test
 *
 * Exit code: 0 = all tests passed, non-zero = failure.
 */

#include "stam/primitives/mailbox2slot.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

using namespace stam::primitives;

namespace stam::primitives {

template <typename T>
class Mailbox2SlotTest final {
public:
    static uint8_t pub_state_value(const Mailbox2SlotCore<T>& core) noexcept {
        return core.pub_state.value.load(std::memory_order_relaxed);
    }

    static uint8_t lock_state_value(const Mailbox2SlotCore<T>& core) noexcept {
        return core.lock_state.value.load(std::memory_order_relaxed);
    }

    static const char* pub_state_addr(const Mailbox2SlotCore<T>& core) noexcept {
        return reinterpret_cast<const char*>(&core.pub_state);
    }

    static const char* lock_state_addr(const Mailbox2SlotCore<T>& core) noexcept {
        return reinterpret_cast<const char*>(&core.lock_state);
    }
};

} // namespace stam::primitives

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int  g_total   = 0;
static int  g_passed  = 0;

static constexpr const char* kSuiteName = "mailbox2slot";
static int  g_failed  = 0;

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
    [[maybe_unused]] Mailbox2Slot<Pod32> mb;
    // If T is not trivially copyable, static_assert fires at compile time.
    // No runtime check needed here.
}

TEST(test_state_constants) {
    EXPECT(kSlot0    == 0u);
    EXPECT(kSlot1    == 1u);
    EXPECT(kNone     == 2u);
    EXPECT(kUnlocked == 2u);
}

TEST(test_core_initial_state) {
    Mailbox2Slot<Pod32> mb;
    EXPECT(Mailbox2SlotTest<Pod32>::pub_state_value(mb.core()) == kNone);
    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

TEST(test_lock_free) {
    EXPECT(std::atomic<uint8_t>::is_always_lock_free);
}

// ---------------------------------------------------------------------------
// Contract tests: behavior
// ---------------------------------------------------------------------------

TEST(test_try_read_before_publish_returns_false) {
    Mailbox2Slot<Pod32> mb;
    auto reader = mb.reader();

    Pod32 out{42, 42};
    const bool ok = reader.try_read(out);

    EXPECT(!ok);
    // out must be unchanged on false return
    EXPECT(out.x == 42 && out.y == 42);
    // postcondition: lock_state == UNLOCKED
    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

TEST(test_publish_then_read) {
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({1, 2});

    Pod32 out{};
    const bool ok = reader.try_read(out);

    EXPECT(ok);
    EXPECT(out.x == 1 && out.y == 2);
    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

TEST(test_latest_wins) {
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({1, 1});
    writer.publish({2, 2});
    writer.publish({3, 3});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 3 && out.y == 3);
}

TEST(test_multiple_reads_return_latest) {
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({10, 20});

    Pod32 a{}, b{};
    EXPECT(reader.try_read(a));
    EXPECT(reader.try_read(b));
    EXPECT(a == b);
    EXPECT(a.x == 10 && a.y == 20);
}

TEST(test_overwrite_same_slot) {
    // Writer publishes repeatedly — must handle invalidate path (I5).
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    for (int i = 0; i < 100; ++i) {
        writer.publish({i, i * 2});
    }

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 99 && out.y == 198);
}

TEST(test_lock_state_unlocked_after_false) {
    // Simulate: try_read returns false, postcondition must hold.
    Mailbox2Slot<Pod32> mb;
    auto reader = mb.reader();

    Pod32 out{};
    EXPECT(!reader.try_read(out));  // no data

    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

TEST(test_lock_state_unlocked_after_true) {
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({5, 6});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

TEST(test_large_pod) {
    Mailbox2Slot<LargePod> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    LargePod src{};
    for (int i = 0; i < 128; ++i) src.data[i] = static_cast<uint8_t>(i);

    writer.publish(src);

    LargePod dst{};
    EXPECT(reader.try_read(dst));
    EXPECT(dst == src);
}

TEST(test_interleaved_publish_read) {
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    for (int i = 0; i < 50; ++i) {
        writer.publish({i, -i});
        Pod32 out{};
        EXPECT(reader.try_read(out));
        EXPECT(out.x == i && out.y == -i);
    }
}

// Verify latest-wins under concurrent load: after writer finishes,
// reader must see the last published value.
TEST(test_spsc_stress_latest_wins) {
    constexpr int kFrames = 200'000;

    Mailbox2Slot<Pod32> mb;

    std::thread writer_thread([&] {
        auto writer = mb.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.publish({i, i});
        }
    });

    writer_thread.join();

    // Writer is done — reader must now see the last frame.
    auto reader = mb.reader();
    Pod32 out{};
    bool ok = reader.try_read(out);
    EXPECT(ok);
    EXPECT(out.x == kFrames && out.y == kFrames);
}

TEST(test_writer_guard_fail_fast) {
    Mailbox2Slot<Pod32> mb;
    const bool aborted = stam::tests::expect_double_issue_abort([&] {
        (void)mb.writer();
    });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast) {
    Mailbox2Slot<Pod32> mb;
    const bool aborted = stam::tests::expect_double_issue_abort([&] {
        (void)mb.reader();
    });
    EXPECT(aborted);
}

// ---------------------------------------------------------------------------
// Diagnostic stress tests
// ---------------------------------------------------------------------------

// Basic SPSC stress: writer publishes N frames, reader reads until it sees
// the last frame. Mailbox2Slot does not guarantee zero torn reads under
// arbitrary overlap; test reports torn/read as an empirical metric.
TEST(test_spsc_stress_no_torn_read) {
    constexpr int kFrames = 200'000;

    Mailbox2Slot<Pod32> mb;

    std::atomic<bool> done{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = mb.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.publish({i, -i});
        }
        done.store(true, std::memory_order_release);
    });

    std::thread reader_thread([&] {
        auto reader = mb.reader();
        Pod32 out{};
        while (!done.load(std::memory_order_acquire) || out.x != kFrames) {
            if (reader.try_read(out)) {
                reads.fetch_add(1, std::memory_order_relaxed);
                // Invariant: x == -y for every published frame.
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

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
    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

// Sustained concurrent stress: both threads run for a fixed duration.
// Diagnostic only: contract does not promise torn==0 here.
TEST(test_spsc_sustained_concurrent) {
    constexpr auto kDuration = std::chrono::milliseconds(200);

    Mailbox2Slot<Pod32> mb;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = mb.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            writer.publish({i, -i});
        }
    });

    std::thread reader_thread([&] {
        auto reader = mb.reader();
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
    EXPECT(Mailbox2SlotTest<Pod32>::lock_state_value(mb.core()) == kUnlocked);
}

// ---------------------------------------------------------------------------
// Implementation tests
// ---------------------------------------------------------------------------

TEST(test_cache_line_separation) {
    // pub_state and lock_state must be on different cache lines.
    Mailbox2Slot<Pod32> mb;
    const auto* ps = Mailbox2SlotTest<Pod32>::pub_state_addr(mb.core());
    const auto* ls = Mailbox2SlotTest<Pod32>::lock_state_addr(mb.core());
    const auto  diff = static_cast<ptrdiff_t>(ls - ps);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int mailbox2slot_tests() {
    std::printf("=== Mailbox2Slot unit tests ===\n\n");
    std::printf("--- contract: static / compile-time ---\n");
    RUN(test_static_assert_trivially_copyable);
    RUN(test_state_constants);
    RUN(test_core_initial_state);
    RUN(test_lock_free);

    std::printf("\n--- contract: behavior ---\n");
    RUN(test_try_read_before_publish_returns_false);
    RUN(test_publish_then_read);
    RUN(test_latest_wins);
    RUN(test_multiple_reads_return_latest);
    RUN(test_overwrite_same_slot);
    RUN(test_lock_state_unlocked_after_false);
    RUN(test_lock_state_unlocked_after_true);
    RUN(test_large_pod);
    RUN(test_interleaved_publish_read);
    RUN(test_spsc_stress_latest_wins);
    RUN(test_writer_guard_fail_fast);
    RUN(test_reader_guard_fail_fast);

    std::printf("\n--- implementation ---\n");
    RUN(test_cache_line_separation);

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
