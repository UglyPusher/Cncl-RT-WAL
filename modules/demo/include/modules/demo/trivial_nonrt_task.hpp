#pragma once
#include <cstdint>
#include <optional>
#include "stam/stam.hpp"
#include "model/tags.hpp"
#include "modules/demo/demo_channel.hpp"

namespace stam::modules::demo {

class trivial_nonrt_task {
public:
    using rt_class = stam::model::rt_unsafe_tag;

    void step(uint32_t now) noexcept;
    [[nodiscard]] stam::model::BindResult bind_port(stam::model::PortName name, demo_reader_t &&reader) noexcept;
    [[nodiscard]] bool is_fully_bound() const noexcept { return sub_.has_value(); }

    [[nodiscard]] uint32_t rx_frames() const noexcept { return rx_frames_; }
    [[nodiscard]] uint32_t last_rt_now() const noexcept { return last_rt_now_; }
    [[nodiscard]] uint32_t last_rt_counter() const noexcept { return last_rt_counter_; }

private:
    uint32_t dummy_{0};
    uint32_t last_rt_now_{0};
    uint32_t last_rt_counter_{0};
    uint32_t rx_frames_{0};
    std::optional<demo_reader_t> sub_{};
};

} // namespace stam::modules::demo
