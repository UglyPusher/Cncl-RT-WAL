#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "exec/tasks/task_wrapper.hpp"
#include "modules/demo/demo_channel.hpp"
#include "modules/demo/trivial_nonrt_task.hpp"
#include "modules/demo/trivial_rt_task.hpp"

using namespace stam;

namespace {

void bind_rt_to_nrt_or_die(modules::demo::trivial_rt_task &rt,
                          modules::demo::trivial_nonrt_task &nrt,
                          modules::demo::demo_channel_t &rt_to_nrt)
{
    (void)rt_to_nrt.bind_writer(rt, modules::demo::k_demo_port_pub);
    (void)rt_to_nrt.bind_reader(nrt, modules::demo::k_demo_port_sub);

    const auto ch_ref = model::make_channel_ref(rt_to_nrt, "rt_to_nrt");
    if (!ch_ref.is_fully_bound_fn(ch_ref.obj))
    {
        std::printf("channel %s is not fully bound\n", ch_ref.name ? ch_ref.name : "?");
        std::exit(1);
    }
}

void print_channel_value(const modules::demo::trivial_nonrt_task &nrt, uint32_t nrt_tick)
{
    if (nrt.rx_frames() == 0)
    {
        std::printf("nrt_tick=%u chan=<empty>\n", nrt_tick);
        return;
    }

    std::printf("nrt_tick=%u chan={now=%u counter=%u} rx=%u\n",
                nrt_tick,
                nrt.last_rt_now(),
                nrt.last_rt_counter(),
                nrt.rx_frames());
}

void run_rt(exec::tasks::TaskWrapper<modules::demo::trivial_rt_task> &w_rt,
            std::atomic<bool> &stop,
            uint32_t rt_steps,
            std::chrono::milliseconds period)
{
    for (uint32_t i = 0; i < rt_steps; ++i)
    {
        w_rt.step(i);
        std::this_thread::sleep_for(period);
    }
    stop.store(true, std::memory_order_release);
}

void run_nonrt(exec::tasks::TaskWrapper<modules::demo::trivial_nonrt_task> &w_nrt,
               modules::demo::trivial_nonrt_task &nrt,
               const std::atomic<bool> &stop,
               std::chrono::milliseconds period)
{
    uint32_t i = 0;
    while (!stop.load(std::memory_order_acquire))
    {
        w_nrt.step(i);
        print_channel_value(nrt, i);
        ++i;
        std::this_thread::sleep_for(period);
    }

    w_nrt.step(i);
    std::printf("nrt_final chan={now=%u counter=%u} rx=%u\n",
                nrt.last_rt_now(),
                nrt.last_rt_counter(),
                nrt.rx_frames());
}

} // namespace

int main()
{
    using namespace std::chrono_literals;

    modules::demo::trivial_rt_task rt{};
    modules::demo::trivial_nonrt_task nrt{};
    modules::demo::demo_channel_t rt_to_nrt{};

    bind_rt_to_nrt_or_die(rt, nrt, rt_to_nrt);

    std::atomic<uint32_t> hb_rt{0};
    std::atomic<uint32_t> hb_nrt{0};

    exec::tasks::TaskWrapper<modules::demo::trivial_rt_task> w_rt{rt};
    exec::tasks::TaskWrapper<modules::demo::trivial_nonrt_task> w_nrt{nrt};
    w_rt.attach_hb(&hb_rt);
    w_nrt.attach_hb(&hb_nrt);

    constexpr uint32_t rt_steps = 10;
    const auto rt_period = 100ms;
    const auto nrt_period = 50ms;

    std::atomic<bool> stop{false};

    std::thread rt_thread{run_rt, std::ref(w_rt), std::ref(stop), rt_steps, rt_period};
    std::thread nrt_thread{run_nonrt, std::ref(w_nrt), std::ref(nrt), std::cref(stop), nrt_period};

    rt_thread.join();
    nrt_thread.join();

    return 0;
}
