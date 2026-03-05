/*
 * spsc_ring_test.cpp
 *
 * Unit tests for SPSCRing (SPSC lock-free ring buffer).
 *
 * Key semantic differences from DoubleBuffer/Mailbox2Slot tested here:
 *  - Queue semantics: every pushed item is delivered in FIFO order
 *  - Intermediate items are NOT dropped (unlike snapshot primitives)
 *  - push() returns false when full (backpressure, not overwrite)
 *  - pop() returns false when empty (no zero-init fallback)
 *  - usable_capacity() == Capacity - 1 (sentinel slot)
 *
 * Build (example):
 *   c++ -std=c++20 -O2 -pthread spsc_ring_test.cpp -o spsc_ring_test
 *
 * Exit code: 0 = all tests passed, non-zero = failure.
 */

#include "spsc_ring.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace stam::exec::primitives;

// ---------------------------------------------------------------------------
// Minimal test harness (same conventions as dbl_buffer_test.cpp)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) static void name()

#define RUN(name)                                          \
    do {                                                   \
        ++g_total;                                         \
        std::printf("  %-55s", #name " ");                 \
        name();                                            \
        ++g_passed;                                        \
        std::printf("PASS\n");                             \
    } while (0)

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

static constexpr size_t kCap = 16;  // power of two, usable = 15

// ---------------------------------------------------------------------------
// Static / compile-time checks
// ---------------------------------------------------------------------------

TEST(test_static_assert_trivially_copyable) {
    [[maybe_unused]] SPSCRing<Pod32, kCap> ring;
}

TEST(test_lock_free) {
    EXPECT(std::atomic<size_t>::is_always_lock_free);
}

TEST(test_core_initial_state) {
    SPSCRing<Pod32, kCap> ring;
    EXPECT(ring.core().head_.load() == 0u);
    EXPECT(ring.core().tail_.load() == 0u);
}

TEST(test_usable_capacity) {
    SPSCRing<Pod32, kCap> ring;
    EXPECT(ring.writer().usable_capacity() == kCap - 1);
    EXPECT(ring.reader().usable_capacity() == kCap - 1);
}

// ---------------------------------------------------------------------------
// Single-threaded functional tests
// ---------------------------------------------------------------------------

// Semantic difference #1: pop() on empty ring returns false, not zero-init T.
TEST(test_pop_empty_returns_false) {
    SPSCRing<Pod32, kCap> ring;
    auto reader = ring.reader();

    Pod32 out{99, 99};
    EXPECT(!reader.pop(out));
    // out must be unchanged on false return
    EXPECT(out.x == 99 && out.y == 99);
}

TEST(test_push_then_pop) {
    SPSCRing<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    EXPECT(writer.push({1, 2}));

    Pod32 out{};
    EXPECT(reader.pop(out));
    EXPECT(out.x == 1 && out.y == 2);
}

// Semantic difference #2: FIFO order — items arrive in push order.
TEST(test_fifo_order) {
    SPSCRing<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    for (int i = 0; i < 5; ++i) {
        EXPECT(writer.push({i, i}));
    }
    for (int i = 0; i < 5; ++i) {
        Pod32 out{};
        EXPECT(reader.pop(out));
        EXPECT(out.x == i && out.y == i);
    }

    // Ring must be empty after draining.
    Pod32 out{};
    EXPECT(!reader.pop(out));
}

// Semantic difference #3: full ring rejects push (no overwrite).
TEST(test_push_full_returns_false) {
    SPSCRing<Pod32, kCap> ring;
    auto writer = ring.writer();

    const size_t cap = writer.usable_capacity();
    for (size_t i = 0; i < cap; ++i) {
        EXPECT(writer.push({static_cast<int32_t>(i), 0}));
    }

    // One more push must fail.
    EXPECT(!writer.push({-1, -1}));
}

TEST(test_fill_drain_fill_again) {
    // Ring must be fully reusable after drain — exercises index wrap-around.
    SPSCRing<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    const size_t cap = writer.usable_capacity();

    for (int round = 0; round < 3; ++round) {
        for (size_t i = 0; i < cap; ++i) {
            EXPECT(writer.push({static_cast<int32_t>(i), round}));
        }
        for (size_t i = 0; i < cap; ++i) {
            Pod32 out{};
            EXPECT(reader.pop(out));
            EXPECT(out.x == static_cast<int32_t>(i));
            EXPECT(out.y == round);
        }
        // Ring must be empty after full drain.
        Pod32 sentinel{};
        EXPECT(!reader.pop(sentinel));
    }
}

TEST(test_empty_full_helpers) {
    SPSCRing<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    EXPECT(reader.empty());
    EXPECT(!writer.full());

    const size_t cap = writer.usable_capacity();
    for (size_t i = 0; i < cap; ++i) {
        writer.push({0, 0});
    }

    EXPECT(!reader.empty());
    EXPECT(writer.full());

    Pod32 out{};
    reader.pop(out);

    EXPECT(!writer.full());
}

TEST(test_interleaved_push_pop) {
    SPSCRing<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    for (int i = 0; i < 50; ++i) {
        EXPECT(writer.push({i, -i}));
        Pod32 out{};
        EXPECT(reader.pop(out));
        EXPECT(out.x == i && out.y == -i);
        EXPECT(reader.empty());
    }
}

TEST(test_large_pod) {
    SPSCRing<LargePod, 8> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    LargePod src{};
    for (int i = 0; i < 128; ++i) src.data[i] = static_cast<uint8_t>(i);

    EXPECT(writer.push(src));

    LargePod dst{};
    EXPECT(reader.pop(dst));
    EXPECT(dst == src);
}

TEST(test_wrap_around) {
    // Push/pop past the physical end of the buffer array.
    SPSCRing<Pod32, 4> ring;  // usable = 3
    auto writer = ring.writer();
    auto reader = ring.reader();

    for (int round = 0; round < 10; ++round) {
        EXPECT(writer.push({round * 10 + 1, 0}));
        EXPECT(writer.push({round * 10 + 2, 0}));
        EXPECT(writer.push({round * 10 + 3, 0}));

        Pod32 a{}, b{}, c{};
        EXPECT(reader.pop(a));
        EXPECT(reader.pop(b));
        EXPECT(reader.pop(c));

        EXPECT(a.x == round * 10 + 1);
        EXPECT(b.x == round * 10 + 2);
        EXPECT(c.x == round * 10 + 3);
    }
}

// ---------------------------------------------------------------------------
// Multi-threaded stress tests
// ---------------------------------------------------------------------------

// All pushed items must arrive, in FIFO order, with no loss.
TEST(test_spsc_stress_fifo_no_loss) {
    constexpr int    kItems   = 200'000;
    constexpr size_t kRingCap = 256;

    SPSCRing<int32_t, kRingCap> ring;

    std::atomic<int> received{0};
    std::atomic<int> order_errors{0};

    std::thread writer_thread([&] {
        auto writer = ring.writer();
        int i = 0;
        while (i < kItems) {
            if (writer.push(i)) ++i;
            // spin on full — acceptable in stress test
        }
    });

    std::thread reader_thread([&] {
        auto reader = ring.reader();
        int expected = 0;
        while (expected < kItems) {
            int32_t val{};
            if (reader.pop(val)) {
                if (val != expected) {
                    order_errors.fetch_add(1, std::memory_order_relaxed);
                }
                ++expected;
            }
        }
        received.store(expected, std::memory_order_release);
    });

    writer_thread.join();
    reader_thread.join();

    EXPECT(received.load() == kItems);
    EXPECT(order_errors.load() == 0);
}

// Each item carries (x, -x); torn read shows as x != -y.
TEST(test_spsc_stress_no_torn_read) {
    constexpr int    kItems   = 200'000;
    constexpr size_t kRingCap = 256;

    SPSCRing<Pod32, kRingCap> ring;
    std::atomic<int> torn{0};

    std::thread writer_thread([&] {
        auto writer = ring.writer();
        int i = 1;
        while (i <= kItems) {
            if (writer.push({i, -i})) ++i;
        }
    });

    std::thread reader_thread([&] {
        auto reader = ring.reader();
        int received = 0;
        while (received < kItems) {
            Pod32 out{};
            if (reader.pop(out)) {
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
                ++received;
            }
        }
    });

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
}

// Sustained concurrent stress for a fixed duration.
TEST(test_spsc_sustained_concurrent) {
    constexpr auto   kDuration = std::chrono::milliseconds(200);
    constexpr size_t kRingCap  = 256;

    SPSCRing<Pod32, kRingCap> ring;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = ring.writer();
        int i = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            if (writer.push({i, -i})) ++i;
        }
    });

    std::thread reader_thread([&] {
        auto reader = ring.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (reader.pop(out)) {
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
}

// ---------------------------------------------------------------------------
// Cache layout checks
// ---------------------------------------------------------------------------

TEST(test_head_tail_on_separate_cache_lines) {
    SPSCRing<Pod32, kCap> ring;
    const auto* h    = reinterpret_cast<const char*>(&ring.core().head_);
    const auto* t    = reinterpret_cast<const char*>(&ring.core().tail_);
    const auto  diff = static_cast<ptrdiff_t>(t - h);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

TEST(test_buffer_separated_from_tail) {
    // pad_ ensures buffer_[0] is not on the same cache line as tail_.
    SPSCRing<Pod32, kCap> ring;
    const auto* t    = reinterpret_cast<const char*>(&ring.core().tail_);
    const auto* buf  = reinterpret_cast<const char*>(&ring.core().buffer_[0]);
    const auto  diff = static_cast<ptrdiff_t>(buf - t);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== SPSCRing unit tests ===\n\n");

    std::printf("--- static / compile-time ---\n");
    RUN(test_static_assert_trivially_copyable);
    RUN(test_lock_free);
    RUN(test_core_initial_state);
    RUN(test_usable_capacity);

    std::printf("\n--- single-threaded functional ---\n");
    RUN(test_pop_empty_returns_false);
    RUN(test_push_then_pop);
    RUN(test_fifo_order);
    RUN(test_push_full_returns_false);
    RUN(test_fill_drain_fill_again);
    RUN(test_empty_full_helpers);
    RUN(test_interleaved_push_pop);
    RUN(test_large_pod);
    RUN(test_wrap_around);

    std::printf("\n--- multi-threaded stress ---\n");
    RUN(test_spsc_stress_fifo_no_loss);
    RUN(test_spsc_stress_no_torn_read);
    RUN(test_spsc_sustained_concurrent);

    std::printf("\n--- cache layout ---\n");
    RUN(test_head_tail_on_separate_cache_lines);
    RUN(test_buffer_separated_from_tail);

    std::printf("\n=== Results: %d/%d passed ===\n", g_passed, g_total);
    return (g_failed == 0) ? 0 : 1;
}
