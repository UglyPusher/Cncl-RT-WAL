#pragma once
#include <atomic>
#include <concepts>
#include <cstdint>


namespace stam::model {

using heartbeat_word_t = uint32_t;
using tick_t           = heartbeat_word_t;

static_assert(std::atomic<heartbeat_word_t>::is_always_lock_free,
    "heartbeat_word_t must be lock-free atomic on this platform");

struct rt_safe_tag {};
// Compatibility marker for explicitly non-RT payloads.
// RtSafe/RtPayload continue to accept only rt_safe_tag.
struct rt_unsafe_tag {};

template<class T>
concept RtSafe =
    requires { typename T::rt_class; } &&
    std::same_as<typename T::rt_class, rt_safe_tag>;

template<class T>
concept Steppable =
    requires(T& t, tick_t now)
{
    { t.step(now) } noexcept -> std::same_as<void>;
};

template<class T>
concept RtHooks =
    (!requires(T& t){ t.init();  } || requires(T& t){ { t.init()  } noexcept; }) &&
    (!requires(T& t){ t.alarm(); } || requires(T& t){ { t.alarm() } noexcept; }) &&
    (!requires(T& t){ t.done();  } || requires(T& t){ { t.done()  } noexcept; });

template<class T>
concept RtPayload =
    RtSafe<T> && Steppable<T> && RtHooks<T>;

} // namespace stam::model
