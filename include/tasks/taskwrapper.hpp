#pragma once
#include <atomic>

namespace tasks {

template <typename Payload>
class TaskWrapper {
public:
    explicit TaskWrapper(Payload& payload, std::atomic<uint32_t>& hb) noexcept
        : payload_(payload)
        , hb_(hb)
    {
        static_assert(
            std::is_same<decltype(std::declval<Payload>().step(0u)), void>::value,
            "Payload must have void step(uint32_t) method"
        );
    }

    void step(uint32_t now) noexcept {
        hb_.store(now, std::memory_order_release);
        payload_.step(now);
    }

private:
    Payload& payload_;
    std::atomic<uint32_t>& hb_;
};

} // namespace tasks