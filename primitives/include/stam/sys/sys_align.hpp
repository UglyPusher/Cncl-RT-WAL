#pragma once
// sys_align.hpp
// Cache-line alignment macros and padding utilities.
#include <cstddef>
#include <cstdint>

#ifndef SYS_CACHELINE_BYTES
  #define SYS_CACHELINE_BYTES 64
#endif

#ifndef SYS_RB_ALIGNMENT
  #define SYS_RB_ALIGNMENT SYS_CACHELINE_BYTES
#endif

#if SYS_CACHELINE_BYTES > 0
  #define SYS_CACHELINE_ALIGN alignas(SYS_CACHELINE_BYTES)
#else
  #define SYS_CACHELINE_ALIGN
#endif

#if SYS_RB_ALIGNMENT > 0
  #define SYS_RB_ALIGN alignas(SYS_RB_ALIGNMENT)
#else
  #define SYS_RB_ALIGN
#endif

// Explicit N-byte padding aligned to a cacheline boundary.
// Use to place adjacent fields (e.g., head / tail) on distinct cache lines.
template <std::size_t N>
struct SYS_CACHELINE_ALIGN sys_pad {
  std::uint8_t bytes[N]; 
};
