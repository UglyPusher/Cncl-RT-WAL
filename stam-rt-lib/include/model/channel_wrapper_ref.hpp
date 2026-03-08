#pragma once
#include <concepts>


namespace stam::model {

struct ChannelRef {
    const void* obj = nullptr;
    bool (*is_fully_bound_fn)(const void*) noexcept = nullptr;
    const char* name = nullptr;
};

template <class C>
concept ChannelLike =
    requires(const C& c)
{
    { c.is_fully_bound() } noexcept -> std::same_as<bool>;
};

template <ChannelLike C>
ChannelRef make_channel_ref(C& ch, const char* name = nullptr) noexcept {
    return {
        &ch,
        [](const void* p) noexcept -> bool {
            return static_cast<const C*>(p)->is_fully_bound();
        },
        name
    };
}

template <ChannelLike C>
ChannelRef make_channel_ref(C&&, const char* = nullptr) noexcept = delete;

} // namespace stam::model
