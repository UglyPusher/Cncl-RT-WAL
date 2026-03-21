#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include "model/tags.hpp"

namespace stam::model
{
    template <size_t MaxTasks>
    class HeartbeatStore final
    {
    public:
        [[nodiscard]] constexpr size_t capacity() const noexcept
        {
            return MaxTasks;
        }

        [[nodiscard]] std::atomic<heartbeat_word_t> *slot(size_t task_id) noexcept
        {
            assert(task_id < MaxTasks);
            return &hb_[task_id];
        }

        [[nodiscard]] heartbeat_word_t load(size_t task_id) const noexcept
        {
            assert(task_id < MaxTasks);
            return hb_[task_id].load(std::memory_order_acquire);
        }

        void reset(heartbeat_word_t value = 0) noexcept
        {
            for (auto &h : hb_)
            {
                h.store(value, std::memory_order_release);
            }
        }

        [[nodiscard]] bool is_alive(size_t task_id,
                                    tick_t now,
                                    tick_t timeout) const noexcept
        {
            const heartbeat_word_t last = load(task_id);
            return static_cast<heartbeat_word_t>(now - last) <= timeout;
        }

    private:
        std::array<std::atomic<heartbeat_word_t>, MaxTasks> hb_{};
    };

} // namespace stam::model
