#include "stam/stam.hpp"
#include "modules/demo/trivial_nonrt_task.hpp"

namespace stam::modules::demo {

void trivial_nonrt_task::step(uint32_t) noexcept
{
    ++dummy_; // simulate work
}

} // namespace stam::modules::demo
