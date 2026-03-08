#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>


namespace stam::sys {

using signal_mask_t =
    std::conditional_t<std::atomic<uint64_t>::is_always_lock_free, uint64_t,
    std::conditional_t<std::atomic<uint32_t>::is_always_lock_free, uint32_t,
    std::conditional_t<std::atomic<uint16_t>::is_always_lock_free, uint16_t,
    uint8_t>>>;

inline constexpr size_t signal_mask_width = sizeof(signal_mask_t) * 8u;

} // namespace stam::sys
