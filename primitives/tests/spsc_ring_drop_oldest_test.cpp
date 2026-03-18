/*
 * spsc_ring_drop_oldest_test.cpp
 *
 * Unit tests for SPSCRingDropOldest (SPSC ring buffer, drop-oldest).
 * Spec: primitives/docs/SPSCRingDropOldest — RT Contract & Invariants.md
 */

#include "stam/primitives/spsc_ring_drop_oldest.hpp"
#include "test_harness.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>


using namespace stam::primitives;

namespace stam::primitives
{

    template <typename T, size_t Capacity>
    class SPSCRingDropOldestTest
    {
        public:
        static size_t get_head(const SPSCRingDropOldestCore<T, Capacity> &core_) noexcept
        {
            return core_.head_.load();
            // return core.pub_state.value.load(std::memory_order_relaxed);
        }
        static size_t get_tail(const SPSCRingDropOldestCore<T, Capacity> &core_) noexcept
        {
            return core_.tail_.load();
            // return core.pub_state.value.load(std::memory_order_relaxed);
        }
    };
}



// ---------------------------------------------------------------------------
// Minimal test harness (same conventions as spsc_ring_test.cpp)
// ---------------------------------------------------------------------------

static int g_total = 0;
static int g_passed = 0;

static constexpr const char *kSuiteName = "spsc_ring_drop_oldest";
static int g_failed = 0;

// TEST/RUN/EXPECT provided by test_harness.hpp

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct Pod32
{
    int32_t x{0};
    int32_t y{0};
    bool operator==(const Pod32 &) const noexcept = default;
};

static constexpr size_t kCap = 8; // power of two, usable = 7

// ---------------------------------------------------------------------------
// Contract tests
// ---------------------------------------------------------------------------

TEST(test_static_assert_trivially_copyable)
{
    [[maybe_unused]] SPSCRingDropOldest<Pod32, kCap> ring;
}

TEST(test_lock_free)
{
    EXPECT(std::atomic<size_t>::is_always_lock_free);
}

TEST(test_core_initial_state)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    using Test_RDO = SPSCRingDropOldestTest<Pod32, kCap>;
    EXPECT(Test_RDO::get_head(ring.core()) == 0u);
    EXPECT(Test_RDO::get_tail(ring.core()) == 0u);
}

TEST(test_usable_capacity)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    EXPECT(ring.writer().usable_capacity() == kCap - 1);
    EXPECT(ring.reader().usable_capacity() == kCap - 1);
}

// ---------------------------------------------------------------------------
// Behavior
// ---------------------------------------------------------------------------

TEST(test_pop_empty_returns_false)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    auto reader = ring.reader();

    Pod32 out{99, 99};
    EXPECT(!reader.pop(out));
    EXPECT(out.x == 99 && out.y == 99);
}

TEST(test_drop_oldest_on_overflow)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    const size_t cap = writer.usable_capacity();
    for (size_t i = 0; i < cap; ++i)
    {
        EXPECT(writer.push({static_cast<int32_t>(i), 0}));
    }

    // One more push drops the oldest element (0).
    EXPECT(!writer.push({99, 0}));

    // Ring now contains 1..(cap-1) plus 99 at the end.
    for (size_t i = 1; i < cap; ++i)
    {
        Pod32 out{};
        EXPECT(reader.pop(out));
        EXPECT(out.x == static_cast<int32_t>(i));
    }

    Pod32 out{};
    EXPECT(reader.pop(out));
    EXPECT(out.x == 99);

    EXPECT(!reader.pop(out));
}

TEST(test_fifo_order_without_overflow)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    auto writer = ring.writer();
    auto reader = ring.reader();

    for (int i = 0; i < 5; ++i)
    {
        EXPECT(writer.push({i, i}));
    }
    for (int i = 0; i < 5; ++i)
    {
        Pod32 out{};
        EXPECT(reader.pop(out));
        EXPECT(out.x == i && out.y == i);
    }

    Pod32 out{};
    EXPECT(!reader.pop(out));
}

TEST(test_writer_guard_fail_fast)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    const bool aborted = stam::tests::expect_double_issue_abort([&]
                                                                { (void)ring.writer(); });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast)
{
    SPSCRingDropOldest<Pod32, kCap> ring;
    const bool aborted = stam::tests::expect_double_issue_abort([&]
                                                                { (void)ring.reader(); });
    EXPECT(aborted);
}

int spsc_ring_drop_oldest_tests()
{
    std::printf("=== SPSCRingDropOldest unit tests ===\n\n");

    RUN(test_static_assert_trivially_copyable);
    RUN(test_lock_free);
    RUN(test_core_initial_state);
    RUN(test_usable_capacity);
    RUN(test_pop_empty_returns_false);
    RUN(test_drop_oldest_on_overflow);
    RUN(test_fifo_order_without_overflow);
    RUN(test_writer_guard_fail_fast);
    RUN(test_reader_guard_fail_fast);

    std::printf("\n[PASS] %d/%d tests passed\n", g_passed, g_total);
    return g_failed;
}
