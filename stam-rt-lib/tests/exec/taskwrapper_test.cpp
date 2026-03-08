/*
 * taskwrapper_test.cpp
 *
 * Tests for TaskWrapper<Payload>.
 * Spec: docs/concepts/fixed/STAM_task_wrapper_structure.md
 *
 * Build: via CMake target stam_exec_tests
 * Exit code: 0 = all tests passed (EXPECT aborts immediately on failure).
 */

#include "exec/tasks/task_wrapper.hpp"
#include "model/tags.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

using stam::exec::tasks::TaskWrapper;
using stam::model::tick_t;
using stam::model::heartbeat_word_t;
using stam::model::Steppable;
using stam::model::RtSafe;
using stam::model::RtHooks;
using stam::model::RtPayload;

// ---------------------------------------------------------------------------
// Minimal test harness (file-local counters)
// ---------------------------------------------------------------------------

static int g_total  = 0;
static int g_passed = 0;

#define TEST(name) static void name()

#define RUN(name)                                              \
    do {                                                       \
        ++g_total;                                             \
        std::printf("  %-60s", #name " ");                     \
        name();                                                \
        ++g_passed;                                            \
        std::printf("PASS\n");                                 \
    } while (0)

// Aborts on failure — intentional: a broken invariant is not recoverable.
#define EXPECT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL\n  assertion failed: %s\n"              \
                        "  at %s:%d\n", #cond, __FILE__, __LINE__);   \
            std::abort();                                              \
        }                                                              \
    } while (0)

// ---------------------------------------------------------------------------
// Mock payload types
// ---------------------------------------------------------------------------

// MinimalPayload: only step() noexcept — satisfies Steppable, not RtPayload
struct MinimalPayload {
    void step(tick_t) noexcept {}
};

// FullPayload: step + rt_safe_tag + all hooks noexcept — satisfies RtPayload
struct FullPayload {
    using rt_class = stam::model::rt_safe_tag;
    void step(tick_t) noexcept {}
    void init()       noexcept {}
    void alarm()      noexcept {}
    void done()       noexcept {}
};

// HookNotNoexcept: alarm() without noexcept — must NOT satisfy RtHooks
struct HookNotNoexcept {
    using rt_class = stam::model::rt_safe_tag;
    void step(tick_t) noexcept {}
    void alarm() {}   // missing noexcept
};

// NoRtTag: step exists but no rt_safe_tag — must NOT satisfy RtSafe
struct NoRtTag {
    void step(tick_t) noexcept {}
};

// SpyPayload: full observability for unit/contract tests
struct SpyPayload {
    tick_t last_step_now = 0;
    int    step_count    = 0;
    int    init_count    = 0;
    int    alarm_count   = 0;
    int    done_count    = 0;

    void step(tick_t now) noexcept { last_step_now = now; ++step_count; }
    void init()           noexcept { ++init_count;  }
    void alarm()          noexcept { ++alarm_count; }
    void done()           noexcept { ++done_count;  }
};

// NoHooksPayload: step only, no init/alarm/done — for "absent" tests
struct NoHooksPayload {
    int step_count = 0;
    void step(tick_t) noexcept { ++step_count; }
};

// ---------------------------------------------------------------------------
// Compile-time checks (group 1)
// Static assertions — verified by the compiler, no RUN needed.
// ---------------------------------------------------------------------------

static_assert( Steppable<MinimalPayload>,  "MinimalPayload must satisfy Steppable");
static_assert(!RtPayload<MinimalPayload>,  "MinimalPayload must NOT satisfy RtPayload (no rt_safe_tag)");
static_assert( RtPayload<FullPayload>,     "FullPayload must satisfy RtPayload");
static_assert(!RtHooks<HookNotNoexcept>,   "HookNotNoexcept must NOT satisfy RtHooks");
static_assert(!RtSafe<NoRtTag>,            "NoRtTag must NOT satisfy RtSafe");

// ---------------------------------------------------------------------------
// Unit tests (group 2)
// ---------------------------------------------------------------------------

TEST(step_calls_payload_step_with_correct_now) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.step(42u);

    EXPECT(p.step_count    == 1);
    EXPECT(p.last_step_now == 42u);
}

TEST(step_updates_heartbeat_to_now) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.step(99u);

    EXPECT(hb.load(std::memory_order_acquire) == 99u);
}

TEST(step_invokes_payload_and_updates_observable_state) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.step(7u);

    EXPECT(p.step_count == 1);
    EXPECT(hb.load(std::memory_order_acquire) == 7u);
}

TEST(init_called_if_present) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.init();

    EXPECT(p.init_count == 1);
}

TEST(init_not_called_if_absent) {
    NoHooksPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<NoHooksPayload> w(p);
    w.attach_hb(&hb);

    w.init();   // must not crash, nothing to call

    EXPECT(p.step_count == 0);
}

TEST(alarm_called_if_present) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.alarm();

    EXPECT(p.alarm_count == 1);
}

TEST(alarm_not_called_if_absent) {
    NoHooksPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<NoHooksPayload> w(p);
    w.attach_hb(&hb);

    w.alarm();  // must not crash

    EXPECT(p.step_count == 0);
}

TEST(done_called_if_present) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.done();

    EXPECT(p.done_count == 1);
}

TEST(done_not_called_if_absent) {
    NoHooksPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<NoHooksPayload> w(p);
    w.attach_hb(&hb);

    w.done();   // must not crash

    EXPECT(p.step_count == 0);
}

// ---------------------------------------------------------------------------
// Contract tests (group 3)
// ---------------------------------------------------------------------------

TEST(heartbeat_updated_on_every_step) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.step(1u);
    EXPECT(hb.load(std::memory_order_acquire) == 1u);

    w.step(2u);
    EXPECT(hb.load(std::memory_order_acquire) == 2u);

    w.step(3u);
    EXPECT(hb.load(std::memory_order_acquire) == 3u);
}

TEST(attach_hb_step_updates_attached_heartbeat) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.step(55u);

    EXPECT(hb.load(std::memory_order_acquire) == 55u);
}

TEST(step_does_not_call_init_alarm_done) {
    SpyPayload p;
    std::atomic<heartbeat_word_t> hb{0};
    TaskWrapper<SpyPayload> w(p);
    w.attach_hb(&hb);

    w.step(1u);
    w.step(2u);

    EXPECT(p.init_count  == 0);
    EXPECT(p.alarm_count == 0);
    EXPECT(p.done_count  == 0);
}

// ---------------------------------------------------------------------------
// Test suite entry point
// ---------------------------------------------------------------------------

void taskwrapper_tests()
{
    std::printf("\n--- TaskWrapper ---\n");

    RUN(step_calls_payload_step_with_correct_now);
    RUN(step_updates_heartbeat_to_now);
    RUN(step_invokes_payload_and_updates_observable_state);
    RUN(init_called_if_present);
    RUN(init_not_called_if_absent);
    RUN(alarm_called_if_present);
    RUN(alarm_not_called_if_absent);
    RUN(done_called_if_present);
    RUN(done_not_called_if_absent);

    RUN(heartbeat_updated_on_every_step);
    RUN(attach_hb_step_updates_attached_heartbeat);
    RUN(step_does_not_call_init_alarm_done);

    std::printf("  passed: %d / %d\n", g_passed, g_total);
}
