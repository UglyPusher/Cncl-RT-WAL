#pragma once
// sys_config.hpp
// Top-level configuration: user overrides, library defaults, and platform header pull-in.
// Include this file (directly or via stam.hpp) to configure the portability layer.

// 1) Optional user override.
// Included automatically if the user places the file on the include path.
#if defined(__has_include)
  #if __has_include("user_sys_config.hpp")
    #include "user_sys_config.hpp"
  #endif
#endif

// 2) Library defaults (applied only if the user has not defined them).
#ifndef SYS_ENABLE_RT
  #define SYS_ENABLE_RT 1
#endif

#ifndef SYS_ASSUME_SINGLE_CORE
  // Typically true for Cortex-M single-core targets; false for SMP.
  #define SYS_ASSUME_SINGLE_CORE 0
#endif

#ifndef SYS_CACHELINE_BYTES
  // Safe default for desktop targets; set to 32 or 0 on Cortex-M (no data cache).
  #define SYS_CACHELINE_BYTES 64
#endif

#ifndef SYS_RB_ALIGNMENT
  // Ring-buffer structure alignment; typically equals the cacheline size.
  #define SYS_RB_ALIGNMENT SYS_CACHELINE_BYTES
#endif

#ifndef SYS_USE_STD_ATOMICS
  #define SYS_USE_STD_ATOMICS 1
#endif

#ifndef SYS_STRICT_RT
  // 1 = enable stricter RT checks and annotations (asserts, attribute markers).
  #define SYS_STRICT_RT 1
#endif

// 3) Pull in the platform-specific headers.
#include "sys/sys_platform.hpp"
#include "sys/sys_compiler.hpp"
#include "sys/sys_arch.hpp"
#include "sys/sys_align.hpp"
#include "sys/sys_fence.hpp"
#include "sys/sys_rt.hpp"
