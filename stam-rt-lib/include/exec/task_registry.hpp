#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <span>
#include <cstdint>
#include "model/tags.hpp"
#include "exec/tasks/task_wrapper_ref.hpp"
#include "model/channel_wrapper_ref.hpp"
#include "model/heartbeat_store.hpp"
#include "stam/sys/sys_signal.hpp"

namespace stam::exec {
using signal_mask_t = stam::sys::signal_mask_t;
inline constexpr size_t SIGNAL_MASK_WIDTH = stam::sys::signal_mask_width;

struct TaskDescriptor
{
    const char *task_name = nullptr;
    stam::exec::tasks::TaskWrapperRef wrapper_ref{};

    uint8_t priority = 0;
    stam::model::tick_t period_ticks = 1;
    stam::model::tick_t last_run_tick = 0;

    static constexpr size_t kInvalidId = static_cast<size_t>(-1);
    size_t bootstrap_index = kInvalidId;
    size_t task_id = kInvalidId;
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

template <size_t MaxTasks = SIGNAL_MASK_WIDTH> class TaskRegistry final
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

        const size_t bootstrap_index = task_count_;
        tasks_[task_count_] = task;
        tasks_[task_count_].bootstrap_index = bootstrap_index;
        tasks_[task_count_].task_id = TaskDescriptor::kInvalidId;
        ++task_count_;
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

        auto end_it = tasks_.begin() + static_cast<std::ptrdiff_t>(task_count_);
        std::stable_sort(tasks_.begin(), end_it,
                         [](const TaskDescriptor &a, const TaskDescriptor &b) noexcept {
                             return a.priority > b.priority;
                         });

        for (auto &v : runtime_id_by_bootstrap_)
            v = TaskDescriptor::kInvalidId;

        for (size_t i = 0; i < task_count_; ++i)
        {
            tasks_[i].task_id = i;
            runtime_id_by_bootstrap_[tasks_[i].bootstrap_index] = i;
        }

        state_ = State::SEALED;
        return {SealResult::Code::ok, nullptr};
    }

    [[nodiscard]] State state() const noexcept { return state_; }

    [[nodiscard]] size_t task_count() const noexcept { return task_count_; }

    [[nodiscard]] const TaskDescriptor *task_by_id(size_t task_id) const noexcept
    {
        if (state_ != State::SEALED)
            return nullptr;
        if (task_id >= task_count_)
            return nullptr;
        return &tasks_[task_id];
    }

    [[nodiscard]] size_t runtime_task_id(size_t bootstrap_index) const noexcept
    {
        if (state_ != State::SEALED)
            return TaskDescriptor::kInvalidId;
        if (bootstrap_index >= task_count_)
            return TaskDescriptor::kInvalidId;
        return runtime_id_by_bootstrap_[bootstrap_index];
    }

    [[nodiscard]] bool bind_heartbeats(stam::model::HeartbeatStore<MaxTasks> &hb) noexcept
    {
        if (state_ != State::SEALED)
            return false;

        for (size_t i = 0; i < task_count_; ++i)
        {
            auto &t = tasks_[i];
            if (t.wrapper_ref.attach_hb_fn == nullptr)
                return false;
            t.wrapper_ref.attach_hb_fn(t.wrapper_ref.obj, hb.slot(i));
        }
        return true;
    }

  private:
    std::array<TaskDescriptor, MaxTasks> tasks_{};
    std::array<size_t, MaxTasks> runtime_id_by_bootstrap_{};
    size_t task_count_ = 0;
    State state_ = State::OPEN;
};

} // namespace stam::exec
