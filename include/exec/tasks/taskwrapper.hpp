#pragma once
#include <atomic>
#include <concepts>
#include <cstdint>
#include "model/tags.hpp"
#include "stam/stam.hpp"


namespace stam::exec::tasks {

template <stam::model::Steppable Payload>
class TaskWrapper {
public:
    explicit TaskWrapper(Payload& payload, std::atomic<uint32_t>& hb) noexcept
        : payload_(payload), hb_(hb)
    {
        
    }

    TaskWrapper(const TaskWrapper&) = delete;
    TaskWrapper& operator=(const TaskWrapper&) = delete;
    TaskWrapper(TaskWrapper&&) = delete;
    TaskWrapper& operator=(TaskWrapper&&) = delete;

    void step(uint32_t now) noexcept {
        payload_.step(now);
        hb_.store(now, std::memory_order_release);
    }

    void init() noexcept {
        if constexpr (requires(Payload& p) { p.init(); }) payload_.init();
    }
    void alarm() noexcept {
        if constexpr (requires(Payload& p) { p.alarm(); }) payload_.alarm();
    }
    void done() noexcept {
        if constexpr (requires(Payload& p) { p.done(); }) payload_.done();
    }

private:
    Payload& payload_;
    std::atomic<uint32_t>& hb_;
};

} // namespace stam::exec::tasks
