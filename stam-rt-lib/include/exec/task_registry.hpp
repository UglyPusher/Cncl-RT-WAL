#pragma once
#include <array>
#include <cstddef>
#include <span>
#include "exec/tasks/task_wrapper_ref.hpp"
#include "model/channel_wrapper_ref.hpp"
#include "stam/sys/sys_signal.hpp"

namespace stam::exec
{
    using signal_mask_t = stam::sys::signal_mask_t;
    inline constexpr size_t SIGNAL_MASK_WIDTH = stam::sys::signal_mask_width;

    struct TaskDescriptor
    {
        const char *task_name = nullptr;
        stam::exec::tasks::TaskWrapperRef wrapper_ref{};
    };

    struct SealResult
    {
        enum class Code : uint8_t
        {
            ok,
            already_sealed,
            task_unbound,
            channel_unbound,
        } code = Code::ok;

        const char *failed_name = nullptr;
    };

    template <size_t MaxTasks = SIGNAL_MASK_WIDTH>
    class TaskRegistry final
    {
    public:
        // TaskRegistry owns task descriptors and validates system readiness at seal():
        // tasks are stored inside registry, channels are passed as transient ChannelRef span.
        // This keeps ownership clear (bootstrap owns channels) while seal checks both sides.
        enum class State : uint8_t
        {
            OPEN,
            SEALED,
        };

        [[nodiscard]] bool add_task(const TaskDescriptor &task) noexcept
        {
            static_assert(MaxTasks <= SIGNAL_MASK_WIDTH,
                          "MaxTasks exceeds signal mask width for this platform");
            if (state_ != State::OPEN)
                return false;
            if (task_count_ >= MaxTasks)
                return false;
            tasks_[task_count_++] = task;
            return true;
        }

        [[nodiscard]] SealResult seal(std::span<const stam::model::ChannelRef> channels) noexcept
        {
            if (state_ == State::SEALED)
            {
                return {SealResult::Code::already_sealed, nullptr};
            }

            for (size_t i = 0; i < task_count_; ++i)
            {
                const auto &t = tasks_[i];
                if (t.wrapper_ref.is_fully_bound_fn == nullptr ||
                    !t.wrapper_ref.is_fully_bound_fn(t.wrapper_ref.obj))
                {
                    return {SealResult::Code::task_unbound, t.task_name};
                }
            }

            for (const auto &c : channels)
            {
                if (c.is_fully_bound_fn == nullptr || !c.is_fully_bound_fn(c.obj))
                {
                    return {SealResult::Code::channel_unbound, c.name};
                }
            }

            state_ = State::SEALED;
            return {SealResult::Code::ok, nullptr};
        }

        [[nodiscard]] State state() const noexcept
        {
            return state_;
        }

        [[nodiscard]] size_t task_count() const noexcept
        {
            return task_count_;
        }

    private:
        std::array<TaskDescriptor, MaxTasks> tasks_{};
        size_t task_count_ = 0;
        State state_ = State::OPEN;
    };

} // namespace stam::exec
