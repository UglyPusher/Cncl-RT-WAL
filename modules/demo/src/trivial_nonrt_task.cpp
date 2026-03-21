#include <utility>
#include "stam/stam.hpp"
#include "modules/demo/trivial_nonrt_task.hpp"

namespace stam::modules::demo {

stam::model::BindResult trivial_nonrt_task::bind_port(stam::model::PortName name, demo_reader_t &&reader) noexcept
{
    if (!(name == k_demo_port_sub))
    {
        return stam::model::BindResult::unknown_port;
    }
    if (sub_.has_value())
    {
        return stam::model::BindResult::already_bound;
    }
    sub_.emplace(std::move(reader));
    return stam::model::BindResult::ok;
}

void trivial_nonrt_task::step(uint32_t) noexcept
{
    ++dummy_; // simulate work

    if (!sub_.has_value())
    {
        return;
    }

    demo_frame frame{};
    if (sub_->try_read(frame))
    {
        last_rt_now_ = frame.now;
        last_rt_counter_ = frame.counter;
        ++rx_frames_;
    }
}

} // namespace stam::modules::demo
