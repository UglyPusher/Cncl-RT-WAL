/*
 * mailbox2slot_smp_test.cpp
 *
 * Stress tests for Mailbox2SlotSmp (SPSC Snapshot Mailbox, SMP-safe).
 * Spec: primitives/docs/Mailbox2SlotSmp - RT Contract & Invariants.md (Rev 1.0)
 *
 * Exit code: 0 = all tests passed (EXPECT aborts immediately on failure).
 */

#include "stam/primitives/mailbox2slot_smp.hpp"
#include "stam/primitives/snapshot_concepts.hpp"
#include "stam/sys/sys_align.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

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

// ---------------------------------------------------------------------------
// Static / compile-time checks
// ---------------------------------------------------------------------------

TEST(test_static_trivially_copyable) {
    [[maybe_unused]] Mailbox2SlotSmp<Pod32> mb;
}

TEST(test_lock_free_atomics) {
    EXPECT(std::atomic<uint32_t>::is_always_lock_free);
    EXPECT(std::atomic<uint8_t>::is_always_lock_free);
    EXPECT(std::atomic<bool>::is_always_lock_free);
}

TEST(test_concepts) {
    static_assert(SnapshotWriter<Mailbox2SlotSmpWriter<Pod32>, Pod32>,
                  "Mailbox2SlotSmpWriter must satisfy SnapshotWriter");
    static_assert(SnapshotReader<Mailbox2SlotSmpReader<Pod32>, Pod32>,
                  "Mailbox2SlotSmpReader must satisfy SnapshotReader");
}

TEST(test_initial_state) {
    Mailbox2SlotSmpCore<Pod32> core;
    EXPECT(!core.ctrl.has_value.load(std::memory_order_relaxed));
    EXPECT(core.ctrl.published.load(std::memory_order_relaxed) == 0u);
    // All seq counters must start at 0 (even = quiescent).
    for (int i = 0; i < 2; ++i) {
        EXPECT(core.seqs[i].seq.load(std::memory_order_relaxed) == 0u);
    }
}

// ---------------------------------------------------------------------------
// Single-threaded functional tests
// ---------------------------------------------------------------------------

TEST(test_try_read_before_publish_returns_false) {
    Mailbox2SlotSmp<Pod32> mb;
    auto reader = mb.reader();

    Pod32 out{42, 42};
    bool ok = reader.try_read(out);

    EXPECT(!ok);
    // out must be unchanged on false return
    EXPECT(out.x == 42 && out.y == 42);
}

TEST(test_publish_then_read) {
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({7, 14});

    Pod32 out{};
    bool ok = reader.try_read(out);
    EXPECT(ok);
    EXPECT(out.x == 7 && out.y == 14);
}

TEST(test_write_alias) {
    // write() alias delegates to publish().
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.write({5, -5});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 5 && out.y == -5);
}

TEST(test_latest_wins) {
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({1, 1});
    writer.publish({2, 2});
    writer.publish({99, 99});

    Pod32 out{};
    EXPECT(reader.try_read(out));
    EXPECT(out.x == 99 && out.y == 99);
}

TEST(test_multiple_reads_return_latest) {
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    writer.publish({10, 20});

    Pod32 a{}, b{};
    EXPECT(reader.try_read(a));
    EXPECT(reader.try_read(b));
    EXPECT(a == b);
    EXPECT(a.x == 10 && a.y == 20);
}

TEST(test_has_value_set_after_first_publish) {
    Mailbox2SlotSmp<Pod32> mb;
    EXPECT(!mb.core().ctrl.has_value.load());
    auto writer = mb.writer();
    writer.publish({1, 1});
    EXPECT(mb.core().ctrl.has_value.load());
}

TEST(test_seq_even_after_publish) {
    // After publish(), both seq counters must be even (write windows closed).
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();

    writer.publish({1, 2});
    for (int i = 0; i < 2; ++i) {
        EXPECT((mb.core().seqs[i].seq.load() & 1u) == 0u);
    }

    writer.publish({3, 4});
    for (int i = 0; i < 2; ++i) {
        EXPECT((mb.core().seqs[i].seq.load() & 1u) == 0u);
    }
}

TEST(test_published_alternates) {
    // Writer always writes to published^1; published must alternate.
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();

    uint8_t prev = mb.core().ctrl.published.load();
    writer.publish({1, 1});
    uint8_t cur = mb.core().ctrl.published.load();
    EXPECT(cur != prev);  // switched
    prev = cur;
    writer.publish({2, 2});
    cur = mb.core().ctrl.published.load();
    EXPECT(cur != prev);  // switched back
}

TEST(test_large_pod) {
    Mailbox2SlotSmp<LargePod> mb;
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
    Mailbox2SlotSmp<Pod32> mb;
    auto writer = mb.writer();
    auto reader = mb.reader();

    for (int i = 1; i <= 50; ++i) {
        writer.publish({i, -i});
        Pod32 out{};
        EXPECT(reader.try_read(out));
        EXPECT(out.x == i && out.y == -i);
    }
}

// ---------------------------------------------------------------------------
// Cache layout checks
// ---------------------------------------------------------------------------

TEST(test_slots_cacheline_aligned) {
    Mailbox2SlotSmp<Pod32> mb;
    for (int i = 0; i < 2; ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(&mb.core().slots[i]);
        EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
    }
}

TEST(test_seqs_cacheline_aligned) {
    Mailbox2SlotSmp<Pod32> mb;
    for (int i = 0; i < 2; ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(&mb.core().seqs[i]);
        EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
    }
}

TEST(test_ctrl_cacheline_aligned) {
    Mailbox2SlotSmp<Pod32> mb;
    const auto addr = reinterpret_cast<uintptr_t>(&mb.core().ctrl);
    EXPECT(addr % SYS_CACHELINE_BYTES == 0u);
}

TEST(test_seq0_separate_from_seq1) {
    // Each seq counter on its own cacheline — no false sharing between them.
    Mailbox2SlotSmp<Pod32> mb;
    const auto a0 = reinterpret_cast<uintptr_t>(&mb.core().seqs[0]);
    const auto a1 = reinterpret_cast<uintptr_t>(&mb.core().seqs[1]);
    const auto diff = static_cast<ptrdiff_t>(a1) - static_cast<ptrdiff_t>(a0);
    EXPECT(std::abs(diff) >= static_cast<ptrdiff_t>(SYS_CACHELINE_BYTES));
}

// ---------------------------------------------------------------------------
// Multi-threaded stress tests
// ---------------------------------------------------------------------------

// Basic SMP stress: writer and reader on separate threads.
// Invariant: for every successfully read pair (i, -i), x == -y.
TEST(test_stress_no_torn_read) {
    constexpr int kFrames = 300'000;

    Mailbox2SlotSmp<Pod32> mb;

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
                if (out.x != -out.y) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    writer_thread.join();
    reader_thread.join();

    EXPECT(torn.load() == 0);
    // After all threads exit, all seq counters must be even.
    for (int i = 0; i < 2; ++i) {
        EXPECT((mb.core().seqs[i].seq.load() & 1u) == 0u);
    }
}

// Latest-wins after writer finishes: reader must see the last published value.
TEST(test_stress_latest_wins_after_writer_done) {
    constexpr int kFrames = 300'000;

    Mailbox2SlotSmp<Pod32> mb;

    std::thread writer_thread([&] {
        auto writer = mb.writer();
        for (int i = 1; i <= kFrames; ++i) {
            writer.publish({i, i});
        }
    });
    writer_thread.join();

    auto reader = mb.reader();
    Pod32 out{};
    bool ok = reader.try_read(out);
    EXPECT(ok);
    EXPECT(out.x == kFrames && out.y == kFrames);
}

// Sustained concurrent stress: both threads run for a fixed duration.
// No torn reads allowed; seq counters must be even after join.
TEST(test_stress_sustained) {
    constexpr auto kDuration = std::chrono::milliseconds(200);

    Mailbox2SlotSmp<Pod32> mb;

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
    for (int i = 0; i < 2; ++i) {
        EXPECT((mb.core().seqs[i].seq.load() & 1u) == 0u);
    }
}

// write() alias stress: same invariants via unified API.
TEST(test_stress_write_alias) {
    constexpr auto kDuration = std::chrono::milliseconds(100);

    Mailbox2SlotSmp<Pod32> mb;

    std::atomic<bool> stop{false};
    std::atomic<int>  torn{0};

    std::thread writer_thread([&] {
        auto writer = mb.writer();
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            ++i;
            writer.write({i, -i});  // unified API
        }
    });

    std::thread reader_thread([&] {
        auto reader = mb.reader();
        Pod32 out{};
        while (!stop.load(std::memory_order_relaxed)) {
            if (reader.try_read(out)) {
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
}

// ---------------------------------------------------------------------------
// Entry point (called from main.cpp)
// ---------------------------------------------------------------------------

void mailbox2slot_smp_tests() {
    std::printf("=== Mailbox2SlotSmp tests ===\n\n");

    std::printf("--- static / compile-time ---\n");
    RUN(test_static_trivially_copyable);
    RUN(test_lock_free_atomics);
    RUN(test_concepts);
    RUN(test_initial_state);

    std::printf("\n--- single-threaded functional ---\n");
    RUN(test_try_read_before_publish_returns_false);
    RUN(test_publish_then_read);
    RUN(test_write_alias);
    RUN(test_latest_wins);
    RUN(test_multiple_reads_return_latest);
    RUN(test_has_value_set_after_first_publish);
    RUN(test_seq_even_after_publish);
    RUN(test_published_alternates);
    RUN(test_large_pod);
    RUN(test_interleaved_publish_read);

    std::printf("\n--- cache layout ---\n");
    RUN(test_slots_cacheline_aligned);
    RUN(test_seqs_cacheline_aligned);
    RUN(test_ctrl_cacheline_aligned);
    RUN(test_seq0_separate_from_seq1);

    std::printf("\n--- multi-threaded stress ---\n");
    RUN(test_stress_no_torn_read);
    RUN(test_stress_latest_wins_after_writer_done);
    RUN(test_stress_sustained);
    RUN(test_stress_write_alias);

    std::printf("\n  passed: %d / %d\n\n", g_passed, g_total);
}
