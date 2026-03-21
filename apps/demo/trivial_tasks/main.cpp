#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>

#include "modules/demo/demo_channel.hpp"
#include "modules/demo/trivial_rt_task.hpp"
#include "modules/demo/trivial_nonrt_task.hpp"
#include "exec/tasks/task_wrapper.hpp"

using namespace stam;

int main()
{
    using namespace std::chrono_literals;

    modules::demo::trivial_rt_task rt{};
    modules::demo::trivial_nonrt_task nrt{};

    modules::demo::demo_channel_t rt_to_nrt{};
    (void)rt_to_nrt.bind_writer(rt, modules::demo::k_demo_port_pub);
    (void)rt_to_nrt.bind_reader(nrt, modules::demo::k_demo_port_sub);

    const auto ch_ref = model::make_channel_ref(rt_to_nrt, "rt_to_nrt");
    if (!ch_ref.is_fully_bound_fn(ch_ref.obj))
    {
        std::printf("channel %s is not fully bound\n", ch_ref.name ? ch_ref.name : "?");
        return 1;
    }

    std::atomic<uint32_t> hb_rt{0};
    std::atomic<uint32_t> hb_nrt{0};

    exec::tasks::TaskWrapper<modules::demo::trivial_rt_task> w_rt{rt};
    exec::tasks::TaskWrapper<modules::demo::trivial_nonrt_task> w_nrt{nrt};
    w_rt.attach_hb(&hb_rt);
    w_nrt.attach_hb(&hb_nrt);

    constexpr uint32_t rt_steps = 10;
    std::atomic<bool> stop{false};

    std::thread rt_thread{[&] {
        for (uint32_t i = 0; i < rt_steps; ++i)
        {
            w_rt.step(i);
            std::this_thread::sleep_for(100ms);
        }
        stop.store(true, std::memory_order_release);
    }};

    std::thread nrt_thread{[&] {
        uint32_t i = 0;
        while (!stop.load(std::memory_order_acquire))
        {
            w_nrt.step(i);

            if (nrt.rx_frames() == 0)
            {
                std::printf("nrt_tick=%u chan=<empty>\n", i);
            }
            else
            {
                std::printf("nrt_tick=%u chan={now=%u counter=%u} rx=%u\n",
                            i,
                            nrt.last_rt_now(),
                            nrt.last_rt_counter(),
                            nrt.rx_frames());
            }

            ++i;
            std::this_thread::sleep_for(50ms); // non-RT runs ~2x faster
        }

        w_nrt.step(i);
        std::printf("nrt_final chan={now=%u counter=%u} rx=%u\n",
                    nrt.last_rt_now(),
                    nrt.last_rt_counter(),
                    nrt.rx_frames());
    }};

    rt_thread.join();
    nrt_thread.join();

    return 0;
}
