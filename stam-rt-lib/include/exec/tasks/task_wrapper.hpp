#pragma once
#include <atomic>
#include <cassert>
#include <concepts>
#include "model/tags.hpp"


namespace stam::exec::tasks {

using stam::model::tick_t;
using stam::model::heartbeat_word_t;

template <class Payload>
requires stam::model::Steppable<Payload>
class TaskWrapper {
public:
    // TaskWrapper is a thin runtime adapter between scheduler and payload:
    // it executes step/hooks and updates heartbeat. Port binding is intentionally
    // outside wrapper (bootstrap phase), so runtime stays minimal and deterministic.
    explicit TaskWrapper(Payload& payload) noexcept
        : payload_(payload)
    {}

    TaskWrapper(const TaskWrapper&) = delete;
    TaskWrapper& operator=(const TaskWrapper&) = delete;
    TaskWrapper(TaskWrapper&&) = delete;
    TaskWrapper& operator=(TaskWrapper&&) = delete;

    void attach_hb(std::atomic<heartbeat_word_t>* hb) noexcept {
        assert(hb  != nullptr);   // реестр обязан передать валидный указатель
        assert(hb_ == nullptr);   // привязка ровно один раз
        hb_ = hb;
    }

    void step(tick_t now) noexcept {
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

    bool is_fully_bound() const noexcept {
        if constexpr (requires(const Payload& p) { { p.is_fully_bound() } noexcept -> std::same_as<bool>; }) {
            return payload_.is_fully_bound();
        } else {
            return true;
        }
    }

private:
    Payload&                       payload_;
    std::atomic<heartbeat_word_t>* hb_ = nullptr;
};

} // namespace stam::exec::tasks
