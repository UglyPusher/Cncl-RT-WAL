#pragma once

#include "task_registry.hpp"

namespace stam::exec
{
    class Scheduler final
    {
    public:
        void start() noexcept { running_ = true; }
        void step() noexcept
        {
            if (running_)
            {
            }
        }
        void stop() noexcept { running_ = false; }
        [[nodiscard]] bool is_running() const noexcept { return running_; }

    private:
        bool running_ = false;
        TaskRegistry<SIGNAL_MASK_WIDTH>* tr_;
    };

} // namespace stam::exec
