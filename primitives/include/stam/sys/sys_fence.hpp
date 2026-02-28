#pragma once
#include <atomic>
#include "sys/sys_compiler.hpp"
#include "sys/sys_arch.hpp"
#include "sys/sys_config.hpp"  // для SYS_ASSUME_SINGLE_CORE

// 1) Compiler fence
SYS_FORCEINLINE void sys_fence_compiler() noexcept {
  sys_compiler_barrier();
}

// 2) Atomic fence (C++ memory model)
SYS_FORCEINLINE void sys_fence_release() noexcept {
  std::atomic_thread_fence(std::memory_order_release);
}

SYS_FORCEINLINE void sys_fence_acquire() noexcept {
  std::atomic_thread_fence(std::memory_order_acquire);
}

SYS_FORCEINLINE void sys_fence_acq_rel() noexcept {
  std::atomic_thread_fence(std::memory_order_acq_rel);
}

SYS_FORCEINLINE void sys_fence_seq_cst() noexcept {
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

// 3) CPU fence (hardware barrier)
SYS_FORCEINLINE void sys_cpu_fence_full() noexcept {
#if SYS_ARCH_ARM && SYS_ASSUME_SINGLE_CORE
  // Single-core Cortex-M: DMB для MMIO
  asm volatile("dmb" ::: "memory");
#elif SYS_ARCH_ARM
  // Multi-core ARM: system-wide barrier
  asm volatile("dmb sy" ::: "memory");
#elif SYS_ARCH_X86
  // x86: full memory fence
  asm volatile("mfence" ::: "memory");
#else
  // Fallback: compiler barrier (no hardware fence)
  sys_compiler_barrier();
#endif
}