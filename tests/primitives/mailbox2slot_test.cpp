/*
 * mailbox2slot_test.cpp
 *
 * Unit tests for Mailbox2Slot (SPSC Snapshot Mailbox).
 * Spec: docs/contracts/Mailbox2Slot.md (Revision 1.3)
 *
 * Build (example):
 *   c++ -std=c++20 -O2 -pthread mailbox2slot_test.cpp -o mailbox2slot_test
 *
 * Exit code: 0 = all tests passed, non-zero = failure.
 */

#include "mailbox2slot.hpp"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace stam::exec::primitives;

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int  g_total   = 0;
static int  g_passed  = 0;
static int  g_failed  = 0;

#define TEST(name) static void name()

#define RUN(name)                                          \
    do {                                                   \
        ++g_total;                                         \
        std::printf("  %-55s", #name " ");                 \
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

// ---------------------------------------------------------------------------
// Static / compile-time checks
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
    EXPECT(mb.core().pub_state.load()  == kNone);
    EXPECT(mb.core().lock_state.load() == kUnlocked);
}

TEST(test_lock_free) {
    EXPECT(std::atomic<uint8_t>::is_always_lock_free);
}

// ---------------------------------------------------------------------------
// Single-threaded functional tests
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
    EXPECT(mb.core().lock_state.load() == kUnlocked);
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
    EXPECT(mb.core().lock_state.load() == kUnlocked);
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
    reader.try_read(out);  // no data

    EXPECT(mb.core().lock_state.load() == kUnlocked);
}

TEST(test_lock_state_unlocked_after_true) {
    Mailbox2Slot<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({5, 6});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(mb.core().lock_state.load() == kUnlocked);
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

// ---------------------------------------------------------------------------
// Multi-threaded stress tests
// ---------------------------------------------------------------------------

// Basic SPSC stress: writer publishes N frames, reader reads until it sees
// the last frame. Verifies no torn reads (x == -y invariant).
TEST(test_spsc_stress_no_torn_read) {
    constexpr int kFrames = 200'000;

    Mailbox2Slot<Pod32> mb;

    std::atomic<bool> done{false};
    std::atomic<int>  torn{0};

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
                // Invariant: x == -y for every published frame.
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
    EXPECT(mb.core().lock_state.load() == kUnlocked);
}

// Verify latest-wins under concurrent load: after writer finishes,
// reader must see the last published value.
TEST(test_spsc_stress_latest_wins) {
    constexpr int kFrames = 200'000;

    Mailbox2Slot<Pod32> mb;
    std::atomic<bool> writer_done{false};

    std::thread writer_thread([&] {
        auto writer = mb.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.publish({i, i});
        }
        writer_done.store(true, std::memory_order_release);
    });

    writer_thread.join();

    // Writer is done — reader must now see the last frame.
    auto reader = mb.reader();
    Pod32 out{};
    bool ok = reader.try_read(out);
    EXPECT(ok);
    EXPECT(out.x == kFrames && out.y == kFrames);
}

// Sustained concurrent stress: both threads run for a fixed duration.
// No torn reads allowed.
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

    EXPECT(torn.load() == 0);
    EXPECT(reads.load() > 0);
    EXPECT(mb.core().lock_state.load() == kUnlocked);
}

// ---------------------------------------------------------------------------
// Cache layout checks
// ---------------------------------------------------------------------------

TEST(test_cache_line_separation) {
    // pub_state and lock_state must be on different cache lines.
    Mailbox2Slot<Pod32> mb;
    const auto* ps = reinterpret_cast<const char*>(&mb.core().pub_state);
    const auto* ls = reinterpret_cast<const char*>(&mb.core().lock_state);
    const auto  diff = static_cast<ptrdiff_t>(ls - ps);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== Mailbox2Slot unit tests ===\n\n");
    std::printf("--- static / compile-time ---\n");
    RUN(test_static_assert_trivially_copyable);
    RUN(test_state_constants);
    RUN(test_core_initial_state);
    RUN(test_lock_free);

    std::printf("\n--- single-threaded functional ---\n");
    RUN(test_try_read_before_publish_returns_false);
    RUN(test_publish_then_read);
    RUN(test_latest_wins);
    RUN(test_multiple_reads_return_latest);
    RUN(test_overwrite_same_slot);
    RUN(test_lock_state_unlocked_after_false);
    RUN(test_lock_state_unlocked_after_true);
    RUN(test_large_pod);
    RUN(test_interleaved_publish_read);

    std::printf("\n--- multi-threaded stress ---\n");
    RUN(test_spsc_stress_no_torn_read);
    RUN(test_spsc_stress_latest_wins);
    RUN(test_spsc_sustained_concurrent);

    std::printf("\n--- cache layout ---\n");
    RUN(test_cache_line_separation);

    std::printf("\n=== Results: %d/%d passed ===\n", g_passed, g_total);
    return (g_failed == 0) ? 0 : 1;
}
