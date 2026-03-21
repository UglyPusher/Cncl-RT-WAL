#include <cstdio>
#include <atomic>

#include "modules/demo/trivial_rt_task.hpp"
#include "modules/demo/trivial_nonrt_task.hpp"
#include "exec/tasks/task_wrapper.hpp"

using namespace stam;

namespace stam {
class base
{
  public:
    int write() { return 0; }
};
class inher : base
{
  public:
    int write() { return 1; }
};
} // namespace stam
int main()
{
    modules::demo::trivial_rt_task rt{};
    modules::demo::trivial_nonrt_task nrt{};

    std::atomic<uint32_t> hb_rt{0};
    std::atomic<uint32_t> hb_nrt{0};

    exec::tasks::TaskWrapper<modules::demo::trivial_rt_task> w_rt{rt};
    exec::tasks::TaskWrapper<modules::demo::trivial_nonrt_task> w_nrt{nrt};
    w_rt.attach_hb(&hb_rt);
    w_nrt.attach_hb(&hb_nrt);
    base bq;
    inher iq;
    std::printf("bq=%u\nbq=%u\n\n\n", bq.write(), iq.write());

    for (uint32_t i = 0; i < 5; ++i)
    {
        w_rt.step(i);
        w_nrt.step(i);
        std::printf("tick=%u\n", i);
    }

    return 0;
}
