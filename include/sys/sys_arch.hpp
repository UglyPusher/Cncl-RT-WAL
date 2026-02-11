#pragma once

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define SYS_ARCH_X86 1
#else
  #define SYS_ARCH_X86 0
#endif

#if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
  #define SYS_ARCH_ARM 1
#else
  #define SYS_ARCH_ARM 0
#endif

// Cortex-M heuristics
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__)
  #define SYS_ARCH_CORTEX_M 1
#else
  #define SYS_ARCH_CORTEX_M 0
#endif
