#pragma once

#include <cstdint>
#include <type_traits>
#include "model/channel_wrapper.hpp"
#include "model/channel_wrapper_ref.hpp"
#include "model/port.hpp"
#include "stam/primitives/mailbox2slot_smp.hpp"

namespace stam::modules::demo {

struct demo_frame final
{
    uint32_t now{0};
    uint32_t counter{0};
};

static_assert(std::is_trivially_copyable_v<demo_frame>);

using demo_primitive_t = stam::primitives::Mailbox2SlotSmp<demo_frame>;
using demo_channel_t = stam::model::ChannelWrapper<demo_primitive_t>;
using demo_writer_t = typename demo_channel_t::writer_t;
using demo_reader_t = typename demo_channel_t::reader_t;

inline constexpr stam::model::PortName k_demo_port_pub{"PUB0"};
inline constexpr stam::model::PortName k_demo_port_sub{"SUB0"};

} // namespace stam::modules::demo
