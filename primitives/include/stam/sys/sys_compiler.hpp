#pragma once

// Conditional include for MSVC intrinsics
#if defined(_MSC_VER)
  #include <intrin.h>
#endif

#if defined(_MSC_VER)
  #define SYS_COMPILER_MSVC 1
#else
  #define SYS_COMPILER_MSVC 0
#endif

#if defined(__clang__)
  #define SYS_COMPILER_CLANG 1
#else
  #define SYS_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !SYS_COMPILER_CLANG
  #define SYS_COMPILER_GCC 1
#else
  #define SYS_COMPILER_GCC 0
#endif

// noinline / forceinline
#if SYS_COMPILER_MSVC
  #define SYS_NOINLINE __declspec(noinline)
  #define SYS_FORCEINLINE __forceinline
#else
  #define SYS_NOINLINE __attribute__((noinline))
  #define SYS_FORCEINLINE inline __attribute__((always_inline))
#endif

// likely/unlikely
#if (SYS_COMPILER_GCC || SYS_COMPILER_CLANG)
  #define SYS_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define SYS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
  #define SYS_LIKELY(x)   (x)
  #define SYS_UNLIKELY(x) (x)
#endif

// Compiler barrier: запретить компилятору переупорядочивать через этот пункт
SYS_FORCEINLINE void sys_compiler_barrier() noexcept {
#if SYS_COMPILER_MSVC
  _ReadWriteBarrier();
#else
  asm volatile("" ::: "memory");
#endif
}
