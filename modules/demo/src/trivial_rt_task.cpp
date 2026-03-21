#include <utility>
#include "stam/stam.hpp"
#include "modules/demo/trivial_rt_task.hpp"

namespace stam::modules::demo {

stam::model::BindResult trivial_rt_task::bind_port(stam::model::PortName name, demo_writer_t &&writer) noexcept
{
    if (!(name == k_demo_port_pub))
    {
        return stam::model::BindResult::unknown_port;
    }
    if (pub_.has_value())
    {
        return stam::model::BindResult::already_bound;
    }
    pub_.emplace(std::move(writer));
    return stam::model::BindResult::ok;
}

void trivial_rt_task::step(uint32_t now) noexcept
{
    ++counter_;

    if (!pub_.has_value())
    {
        return;
    }
    pub_->write(demo_frame{
        .now = now,
        .counter = counter_,
    });
}

} // namespace stam::modules::demo
