#include "./sys_preempt_lock.hpp"

namespace stam::sys {
void sys_preemption_disable_impl() noexcept {}
void sys_preemption_enable_impl() noexcept {}
} // namespace stam::sys
