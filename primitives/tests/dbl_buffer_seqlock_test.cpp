/*
 * dbl_buffer_seqlock_test.cpp
 *
 * Stress tests for DoubleBufferSeqLock (SPSC Snapshot Buffer, SMP-safe).
 * Spec: primitives/docs/DoubleBufferSeqLock - RT Contract & Invariants.md (Rev 1.0)
 *
 * Exit code: 0 = all tests passed (EXPECT aborts immediately on failure).
 */

#include "stam/primitives/dbl_buffer_seqlock.hpp"
#include "stam/primitives/snapshot_concepts.hpp"
#include "stam/sys/sys_align.hpp"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

using namespace stam::primitives;

// ---------------------------------------------------------------------------
// Minimal test harness (file-local counters)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;

#define TEST(name) static void name()

#define RUN(name)                                            \
    do {                                                     \
        ++g_total;                                           \
        std::printf("  %-55s", #name " ");                   \
        name();                                              \
        ++g_passed;                                          \
        std::printf("PASS\n");                               \
    } while (0)

#define EXPECT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL\n  assertion failed: %s\n"              \
                        "  at %s:%d\n", #cond, __FILE__, __LINE__);   \
            std::abort();                                              \
        }                                                              \
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
    [[maybe_unused]] DoubleBufferSeqLock<Pod32> ch;
}

TEST(test_lock_free_atomics) {
    EXPECT(std::atomic<uint32_t>::is_always_lock_free);
}

TEST(test_concepts) {
    // Verify SnapshotWriter / SnapshotReader concepts are satisfied.
    static_assert(SnapshotWriter<DoubleBufferSeqLockWriter<Pod32>, Pod32>,
                  "DoubleBufferSeqLockWriter must satisfy SnapshotWriter");
    static_assert(SnapshotReader<DoubleBufferSeqLockReader<Pod32>, Pod32>,
                  "DoubleBufferSeqLockReader must satisfy SnapshotReader");
}

TEST(test_initial_state_zero) {
    // Before first write(), read() returns value-initialized (zero) T.
    DoubleBufferSeqLock<Pod32> ch;
    auto reader = ch.reader();
    Pod32 out{42, 42};
    reader.read(out);
    EXPECT(out.x == 0 && out.y == 0);
}

TEST(test_try_read_before_write_returns_true) {
    // try_read() on DoubleBufferSeqLock always returns true (no "no data" sentinel).
    DoubleBufferSeqLock<Pod32> ch;
    auto reader = ch.reader();
    Pod32 out{};
    EXPECT(reader.try_read(out) == true);
}

TEST(test_seq_initial_value) {
    // seq starts at 0 (even = quiescent).
    DoubleBufferSeqLock<Pod32> ch;
    EXPECT(ch.core().ctrl.seq.load(std::memory_order_relaxed) == 0u);
}

// ---------------------------------------------------------------------------
// Single-threaded functional tests
// ---------------------------------------------------------------------------

TEST(test_write_then_read) {
    DoubleBufferSeqLock<Pod32> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({7, 14});

    Pod32 out{};
    reader.read(out);
    EXPECT(out.x == 7 && out.y == 14);
}

TEST(test_try_read_after_write) {
    DoubleBufferSeqLock<Pod32> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({3, -3});

    Pod32 out{};
    bool ok = reader.try_read(out);
    EXPECT(ok);
    EXPECT(out.x == 3 && out.y == -3);
}

TEST(test_latest_wins) {
    DoubleBufferSeqLock<Pod32> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({1, 1});
    writer.write({2, 2});
    writer.write({99, 99});

    Pod32 out{};
    reader.read(out);
    EXPECT(out.x == 99 && out.y == 99);
}

TEST(test_seq_even_after_write) {
    // After write(), seq must be even again (write closed).
    DoubleBufferSeqLock<Pod32> ch;
    auto writer = ch.writer();
    writer.write({1, 2});
    EXPECT((ch.core().ctrl.seq.load(std::memory_order_relaxed) & 1u) == 0u);
}

TEST(test_multiple_reads_return_latest) {
    DoubleBufferSeqLock<Pod32> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({10, 20});

    Pod32 a{}, b{};
    reader.read(a);
    reader.read(b);
    EXPECT(a == b);
    EXPECT(a.x == 10 && a.y == 20);
}

TEST(test_large_pod) {
    DoubleBufferSeqLock<LargePod> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    LargePod src{};
    for (int i = 0; i < 128; ++i) src.data[i] = static_cast<uint8_t>(i);

    writer.write(src);

    LargePod dst{};
    reader.read(dst);
    EXPECT(dst == src);
}

TEST(test_write_alias) {
    // write() and the canonical write() are the same for SeqLock.
    DoubleBufferSeqLock<Pod32> ch;
    auto writer = ch.writer();
    auto reader = ch.reader();

    writer.write({55, -55});

    Pod32 out{};
    reader.read(out);
    EXPECT(out.x == 55 && out.y == -55);
}

TEST(test_writer_guard_fail_fast) {
    const bool aborted = expect_child_abort([] {
        DoubleBufferSeqLock<Pod32> ch;
        (void)ch.writer();
        (void)ch.writer();
    });
    EXPECT(aborted);
}

TEST(test_reader_guard_fail_fast) {
    const bool aborted = expect_child_abort([] {
        DoubleBufferSeqLock<Pod32> ch;
        (void)ch.reader();
        (void)ch.reader();
    });
    EXPECT(aborted);
}

// ---------------------------------------------------------------------------
// Cache layout checks
// ---------------------------------------------------------------------------

TEST(test_seq_cacheline_alignment) {
    DoubleBufferSeqLock<Pod32> ch;
    const auto addr = reinterpret_cast<uintptr_t>(&ch.core().ctrl);
    EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
}

TEST(test_slot_cacheline_alignment) {
    DoubleBufferSeqLock<Pod32> ch;
    const auto addr = reinterpret_cast<uintptr_t>(&ch.core().slot);
    EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
}

TEST(test_seq_separate_from_slot) {
    // seq and slot must be on separate cachelines.
    DoubleBufferSeqLock<Pod32> ch;
    const auto seq_addr  = reinterpret_cast<uintptr_t>(&ch.core().ctrl);
    const auto slot_addr = reinterpret_cast<uintptr_t>(&ch.core().slot);
    const auto diff = static_cast<ptrdiff_t>(seq_addr) -
                      static_cast<ptrdiff_t>(slot_addr);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

// ---------------------------------------------------------------------------
// Multi-threaded stress tests
// ---------------------------------------------------------------------------

// Basic SMP stress: writer and reader on separate threads.
// Invariant: for every successfully published pair (i, -i), reader sees x == -y.
TEST(test_stress_no_torn_read) {
    constexpr int kFrames = 300'000;

    DoubleBufferSeqLock<Pod32> ch;

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
            reader.read(out);
            if (out.x != 0 && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
}

// try_read() stress: verify unified API never returns false under concurrent write.
TEST(test_stress_try_read_no_torn_read) {
    constexpr auto kDuration = std::chrono::milliseconds(150);

    DoubleBufferSeqLock<Pod32> ch;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            writer.write({i, -i});
        }
    });

    std::thread reader_thread([&] {
        auto reader = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            bool ok = reader.try_read(out);
            EXPECT(ok);  // always true for SeqLock
            if (out.x != 0 && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
}

// Latest-wins after writer finishes: reader must see the last published value.
TEST(test_stress_latest_wins_after_writer_done) {
    constexpr int kFrames = 300'000;

    DoubleBufferSeqLock<Pod32> ch;

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.write({i, i});
        }
    });
    writer_thread.join();

    auto reader = ch.reader();
    Pod32 out{};
    reader.read(out);
    EXPECT(out.x == kFrames && out.y == kFrames);
}

// Sustained concurrent stress: both threads run for a fixed duration.
TEST(test_stress_sustained) {
    constexpr auto kDuration = std::chrono::milliseconds(200);

    DoubleBufferSeqLock<Pod32> ch;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};
    std::atomic<int>  reads{0};

    std::thread writer_thread([&] {
        auto writer = ch.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            writer.write({i, -i});
        }
    });

    std::thread reader_thread([&] {
        auto reader = ch.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            reader.read(out);
            reads.fetch_add(1, std::memory_order_relaxed);
            if (out.x != 0 && out.x != -out.y) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_release);

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
    EXPECT(reads.load() > 0);
    // seq must be even (write closed) after all threads exit.
    EXPECT((ch.core().ctrl.seq.load() & 1u) == 0u);
}

// ---------------------------------------------------------------------------
// Entry point (called from main.cpp)
// ---------------------------------------------------------------------------

void dbl_buffer_seqlock_tests() {
    std::printf("=== DoubleBufferSeqLock tests ===\n\n");

    std::printf("--- static / compile-time ---\n");
    RUN(test_static_trivially_copyable);
    RUN(test_lock_free_atomics);
    RUN(test_concepts);
    RUN(test_initial_state_zero);
    RUN(test_try_read_before_write_returns_true);
    RUN(test_seq_initial_value);

    std::printf("\n--- single-threaded functional ---\n");
    RUN(test_write_then_read);
    RUN(test_try_read_after_write);
    RUN(test_latest_wins);
    RUN(test_seq_even_after_write);
    RUN(test_multiple_reads_return_latest);
    RUN(test_large_pod);
    RUN(test_write_alias);
    RUN(test_writer_guard_fail_fast);
    RUN(test_reader_guard_fail_fast);

    std::printf("\n--- cache layout ---\n");
    RUN(test_seq_cacheline_alignment);
    RUN(test_slot_cacheline_alignment);
    RUN(test_seq_separate_from_slot);

    std::printf("\n--- multi-threaded stress ---\n");
    RUN(test_stress_no_torn_read);
    RUN(test_stress_try_read_no_torn_read);
    RUN(test_stress_latest_wins_after_writer_done);
    RUN(test_stress_sustained);

    std::printf("\n  passed: %d / %d\n\n", g_passed, g_total);
}
