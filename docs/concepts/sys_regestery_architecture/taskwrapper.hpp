#pragma once
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include "model/tags.hpp"
#include "stam/stam.hpp"


namespace stam::exec::tasks {

template <stam::model::Steppable Payload>
class TaskWrapper {
public:
    explicit TaskWrapper(Payload& payload) noexcept
        : payload_(payload)
    {

    }

    TaskWrapper(const TaskWrapper&) = delete;
    TaskWrapper& operator=(const TaskWrapper&) = delete;
    TaskWrapper(TaskWrapper&&) = delete;
    TaskWrapper& operator=(TaskWrapper&&) = delete;

    void attach_hb(std::atomic<stam::model::heartbeat_word_t>* hb) noexcept {
        assert(hb != nullptr);
        assert(hb_ == nullptr);
        hb_ = hb;
    }

    void step(stam::model::tick_t now) noexcept {
        assert(hb_ != nullptr);
        payload_.step(now);
        hb_->store(now, std::memory_order_release);
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
    std::atomic<stam::model::heartbeat_word_t>* hb_ = nullptr;
};

} // namespace stam::exec::tasks
