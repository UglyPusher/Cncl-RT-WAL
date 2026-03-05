#pragma once
#include <cstdint>
#include "stam/stam.hpp"
#include "model/tags.hpp"

namespace stam::modules::demo {

class trivial_rt_task {
public:
    using rt_class = stam::model::rt_safe_tag;

    void step(uint32_t now) noexcept;

private:
    uint32_t counter_{0};
};

} // namespace stam::modules::demo
