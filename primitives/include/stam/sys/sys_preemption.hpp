#pragma once

/*
    sys_preemption.hpp
    ------------------

    Platform abstraction for preemption control.

    This header declares the interface used by RT primitives to temporarily
    disable and enable preemption (typically IRQ masking or scheduler lock).

    The actual implementation MUST be provided by the platform.

    Rationale
    ---------

    RT primitives may require non-reentrant execution contexts.
    On many platforms this is achieved by temporarily disabling
    preemption (for example by masking interrupts).

    This file intentionally provides NO default implementation.

    Missing implementation is a configuration error and must
    result in a linker failure.

    This avoids silent misconfiguration where preemption control
    is expected but not actually performed.

    Typical implementations:

        Cortex-M:
            __disable_irq()
            __enable_irq()

        RTOS:
            scheduler_lock()
            scheduler_unlock()

        Bare-metal cooperative:
            may be implemented as no-op if reentrancy is impossible.


    Platform requirements
    ---------------------

    The platform must define:

        stam::sys::sys_preemption_disable_impl()
        stam::sys::sys_preemption_enable_impl()

    Both functions must:

        - be noexcept
        - be bounded O(1)
        - perform no allocations
        - perform no blocking operations


    Example platform implementation
    -------------------------------

        #include <cmsis_gcc.h>

        namespace stam::sys
        {

        void sys_preemption_disable_impl() noexcept
        {
            __disable_irq();
        }

        void sys_preemption_enable_impl() noexcept
        {
            __enable_irq();
        }

        }


    Failure mode
    ------------

    If the platform does not provide implementations,
    the linker will produce an error:

        undefined reference to
            stam::sys::sys_preemption_disable_impl()

    This is intentional and indicates incomplete platform port.
*/

namespace stam::sys
{

//--------------------------------------------------
// Platform-provided implementation
//--------------------------------------------------

void sys_preemption_disable_impl() noexcept;
void sys_preemption_enable_impl() noexcept;


//--------------------------------------------------
// Public API
//--------------------------------------------------

/*
    Disable preemption for the current execution context.

    Typically implemented as:

        - IRQ masking
        - scheduler lock
        - critical section entry

    Requirements:

        - bounded execution time
        - no blocking
        - no allocation
        - noexcept

    Must be paired with preemption_enable().
*/

inline void preemption_disable() noexcept
{
    sys_preemption_disable_impl();
}


/*
    Re-enable preemption.

    Must restore the state established by preemption_disable().

    Requirements:

        - bounded execution time
        - no blocking
        - no allocation
        - noexcept
*/

inline void preemption_enable() noexcept
{
    sys_preemption_enable_impl();
}

} // namespace stam::sys