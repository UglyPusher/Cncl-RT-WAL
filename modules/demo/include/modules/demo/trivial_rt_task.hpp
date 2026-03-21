#pragma once
#include <cstdint>
#include <optional>
#include "stam/stam.hpp"
#include "model/tags.hpp"
#include "modules/demo/demo_channel.hpp"

namespace stam::modules::demo {

class trivial_rt_task {
public:
    using rt_class = stam::model::rt_safe_tag;

    void step(uint32_t now) noexcept;
    [[nodiscard]] stam::model::BindResult bind_port(stam::model::PortName name, demo_writer_t &&writer) noexcept;
    [[nodiscard]] bool is_fully_bound() const noexcept { return pub_.has_value(); }

private:
    uint32_t counter_{0};
    std::optional<demo_writer_t> pub_{};
};

} // namespace stam::modules::demo
