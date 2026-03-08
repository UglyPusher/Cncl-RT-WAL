#include "model/channel_wrapper_ref.hpp"
#include "exec/task_registry.hpp"
#include "exec/tasks/task_wrapper.hpp"
#include "exec/tasks/task_wrapper_ref.hpp"

#include <span>
#include <cstdio>
#include <cstdlib>

using stam::model::ChannelRef;
using stam::exec::SealResult;
using stam::exec::TaskDescriptor;
using stam::exec::TaskRegistry;
using stam::model::tick_t;
using stam::exec::tasks::TaskWrapper;
using stam::exec::tasks::make_task_wrapper_ref;

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

#define EXPECT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL\n  assertion failed: %s\n"              \
                        "  at %s:%d\n", #cond, __FILE__, __LINE__);   \
            std::abort();                                              \
        }                                                              \
    } while (0)

struct BoundPayload {
    bool bound = true;
    void step(tick_t) noexcept {}
    bool is_fully_bound() const noexcept { return bound; }
};

struct NoPortsPayload {
    void step(tick_t) noexcept {}
};

struct FakeChannel {
    bool ok = false;
    bool is_fully_bound() const noexcept { return ok; }
};

TEST(seal_ok_when_tasks_and_channels_bound) {
    BoundPayload p;
    TaskWrapper<BoundPayload> w(p);
    auto ref = make_task_wrapper_ref(w);

    TaskRegistry<4> reg;
    EXPECT(reg.add_task(TaskDescriptor{"TASK_A", ref}));

    FakeChannel ch{true};
    ChannelRef refs[] = { stam::model::make_channel_ref(ch, "CH_A") };

    const auto r = reg.seal(refs);
    EXPECT(r.code == SealResult::Code::ok);
    EXPECT(r.failed_name == nullptr);
    EXPECT(reg.state() == TaskRegistry<4>::State::SEALED);
}

TEST(seal_fails_on_unbound_task) {
    BoundPayload p;
    p.bound = false;
    TaskWrapper<BoundPayload> w(p);
    auto ref = make_task_wrapper_ref(w);

    TaskRegistry<4> reg;
    EXPECT(reg.add_task(TaskDescriptor{"TASK_B", ref}));

    const auto r = reg.seal(std::span<const ChannelRef>{});
    EXPECT(r.code == SealResult::Code::task_unbound);
    EXPECT(r.failed_name != nullptr);
}

TEST(seal_fails_on_unbound_channel) {
    BoundPayload p;
    TaskWrapper<BoundPayload> w(p);
    auto ref = make_task_wrapper_ref(w);

    TaskRegistry<4> reg;
    EXPECT(reg.add_task(TaskDescriptor{"TASK_C", ref}));

    FakeChannel ch{false};
    ChannelRef refs[] = { stam::model::make_channel_ref(ch, "CH_BAD") };

    const auto r = reg.seal(refs);
    EXPECT(r.code == SealResult::Code::channel_unbound);
    EXPECT(r.failed_name != nullptr);
}

TEST(seal_is_idempotent_with_error_on_second_call) {
    NoPortsPayload p;
    TaskWrapper<NoPortsPayload> w(p);
    auto ref = make_task_wrapper_ref(w);

    TaskRegistry<4> reg;
    EXPECT(reg.add_task(TaskDescriptor{"TASK_D", ref}));

    const auto r1 = reg.seal(std::span<const ChannelRef>{});
    EXPECT(r1.code == SealResult::Code::ok);

    const auto r2 = reg.seal(std::span<const ChannelRef>{});
    EXPECT(r2.code == SealResult::Code::already_sealed);
}

void task_registry_tests()
{
    std::printf("\n--- TaskRegistry ---\n");

    RUN(seal_ok_when_tasks_and_channels_bound);
    RUN(seal_fails_on_unbound_task);
    RUN(seal_fails_on_unbound_channel);
    RUN(seal_is_idempotent_with_error_on_second_call);

    std::printf("  passed: %d / %d\n", g_passed, g_total);
}
