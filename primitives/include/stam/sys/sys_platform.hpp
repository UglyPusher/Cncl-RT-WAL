#pragma once

// sys_platform.hpp
// Platform/OS/RTOS feature detection + user hooks.
// Intentionally lightweight: no inline asm, no heavy headers.

#include <cstddef>
#include <cstdint>

//------------------------------------------------------------------------------
// OS detection
//------------------------------------------------------------------------------

#if defined(_WIN32) || defined(_WIN64)
  #define SYS_OS_WINDOWS 1
#else
  #define SYS_OS_WINDOWS 0
#endif

#if defined(__linux__)
  #define SYS_OS_LINUX 1
#else
  #define SYS_OS_LINUX 0
#endif

#if defined(__APPLE__)
  #define SYS_OS_APPLE 1
#else
  #define SYS_OS_APPLE 0
#endif

#if defined(__unix__) || defined(__unix)
  #define SYS_OS_UNIX 1
#else
  #define SYS_OS_UNIX 0
#endif

// Heuristic: bare-metal / freestanding
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0)
  #define SYS_BARE_METAL 1
#else
  #define SYS_BARE_METAL 0
#endif

//------------------------------------------------------------------------------
// RTOS / environment hints (user may override in user_sys_config.hpp)
//------------------------------------------------------------------------------

// If you have a specific RTOS macro, define SYS_HAS_RTOS=1 in user_sys_config.hpp
#ifndef SYS_HAS_RTOS
  #define SYS_HAS_RTOS 0
#endif

// Threads availability (C++ std::thread / OS threads).
// For bare-metal: default 0, for hosted OS: default 1.
#ifndef SYS_HAS_THREADS
  #if SYS_BARE_METAL
    #define SYS_HAS_THREADS 0
  #else
    #define SYS_HAS_THREADS 1
  #endif
#endif

//------------------------------------------------------------------------------
// Endianness
//------------------------------------------------------------------------------

#ifndef SYS_ENDIAN_LITTLE
  #define SYS_ENDIAN_LITTLE 1234
#endif
#ifndef SYS_ENDIAN_BIG
  #define SYS_ENDIAN_BIG 4321
#endif

#ifndef SYS_BYTE_ORDER
  // Prefer compiler-provided macros when available.
  #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
    #if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
      #define SYS_BYTE_ORDER SYS_ENDIAN_LITTLE
    #elif (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
      #define SYS_BYTE_ORDER SYS_ENDIAN_BIG
    #else
      #error "Unsupported __BYTE_ORDER__ value"
    #endif
  #elif SYS_OS_WINDOWS
    // Windows is little-endian on supported targets.
    #define SYS_BYTE_ORDER SYS_ENDIAN_LITTLE
  #else
    // Fallback: assume little-endian (common), but allow user override.
    #define SYS_BYTE_ORDER SYS_ENDIAN_LITTLE
  #endif
#endif

#if (SYS_BYTE_ORDER == SYS_ENDIAN_LITTLE)
  #define SYS_IS_LITTLE_ENDIAN 1
  #define SYS_IS_BIG_ENDIAN 0
#elif (SYS_BYTE_ORDER == SYS_ENDIAN_BIG)
  #define SYS_IS_LITTLE_ENDIAN 0
  #define SYS_IS_BIG_ENDIAN 1
#else
  #error "SYS_BYTE_ORDER must be SYS_ENDIAN_LITTLE or SYS_ENDIAN_BIG"
#endif

//------------------------------------------------------------------------------
// Cache / page size (hints only; user can override)
//------------------------------------------------------------------------------

// 1 if data cache exists and can produce false sharing. On many Cortex-M: 0.
// User should override per target.
#ifndef SYS_HAS_DATA_CACHE
  #if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
      defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)
    // Many Cortex-M have no D-cache by default; M7/M33 variants may have.
// Keep conservative: assume 0 unless user says otherwise.
    #define SYS_HAS_DATA_CACHE 0
  #else
    // Hosted CPUs typically have cache.
    #define SYS_HAS_DATA_CACHE 1
  #endif
#endif

// Page size is mostly relevant for virtual-memory OS (allocation/backends).
#ifndef SYS_PAGE_SIZE
  #if SYS_OS_WINDOWS
    // Common, but not guaranteed; treat as hint.
    #define SYS_PAGE_SIZE 4096u
  #elif SYS_OS_LINUX || SYS_OS_APPLE || SYS_OS_UNIX
    #define SYS_PAGE_SIZE 4096u
  #else
    #define SYS_PAGE_SIZE 0u
  #endif
#endif

//------------------------------------------------------------------------------
// RT context hook
//------------------------------------------------------------------------------
//
// If you can detect "we are in RT context" (ISR, high-priority task, RT section),
// define SYS_HAS_IN_RT_CONTEXT=1 and provide:
//
//   extern "C" bool sys_in_rt_context_impl() noexcept;
//
// The sys_rt.hpp layer can call it (or you can route from there).
//
#ifndef SYS_HAS_IN_RT_CONTEXT
  #define SYS_HAS_IN_RT_CONTEXT 0
#endif

// Optional: ISR context hook (useful even without threads)
#ifndef SYS_HAS_IN_ISR_CONTEXT
  #define SYS_HAS_IN_ISR_CONTEXT 0
#endif

//------------------------------------------------------------------------------
// Misc: noexcept policy / assertions (lightweight knobs)
//------------------------------------------------------------------------------

// Some environments want to disable asserts in RT builds.
#ifndef SYS_ENABLE_ASSERTS
  #if defined(NDEBUG)
    #define SYS_ENABLE_ASSERTS 0
  #else
    #define SYS_ENABLE_ASSERTS 1
  #endif
#endif
