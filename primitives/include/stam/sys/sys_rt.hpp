#pragma once
#include <cstdint>
#include "sys/sys_compiler.hpp"

#ifndef SYS_STRICT_RT
  #define SYS_STRICT_RT 1
#endif

#ifndef SYS_HAS_IN_RT_CONTEXT
  #define SYS_HAS_IN_RT_CONTEXT 0
#endif

// User provides this function (if enabled)
#if SYS_HAS_IN_RT_CONTEXT
  extern bool sys_platform_in_rt_context() noexcept;
  //          ^^^^^^^^^^^^^^^^^^^^^^^^^^^ user-provided implementation
#endif

SYS_FORCEINLINE bool sys_in_rt_context() noexcept {
#if SYS_HAS_IN_RT_CONTEXT
  return sys_platform_in_rt_context();  // wrapper
#else
  return false; // default: unknown
#endif
}

// RT-safe assert (NO I/O, NO stdlib abort)
#if SYS_STRICT_RT && !defined(NDEBUG)
  #define SYS_RT_ASSERT(x) do { \
    if (SYS_UNLIKELY(!(x))) { \
      while(1) { /* halt CPU */ } \
    } \
  } while(0)
#else
  #define SYS_RT_ASSERT(x) do { (void)sizeof(x); } while(0)
#endif

// Annotation for code review
#define SYS_RT_SECTION /* marker for RT-critical code */