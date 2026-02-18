#include "stam/stam.hpp"
#include "modules/demo/trivial_rt_task.hpp"

namespace stam::modules::demo {

void trivial_rt_task::step(uint32_t) noexcept
{
    ++counter_;
}

} // namespace stam::modules::demo
