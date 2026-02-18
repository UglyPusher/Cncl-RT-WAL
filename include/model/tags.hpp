#pragma once
#include <concepts>
#include <cstdint>


namespace stam::model {
    struct rt_safe_tag {};
    struct rt_unsafe_tag {};

template<class T>
concept RtSafe =
    requires { typename T::rt_class; } &&
    std::same_as<typename T::rt_class, rt_safe_tag>;

template<class T>
concept Steppable =
    requires(T& t, uint32_t now)
{
    { t.step(now) } noexcept -> std::same_as<void>;
};

template<class T>
concept RtPayload =
    RtSafe<T> && Steppable<T>;
}