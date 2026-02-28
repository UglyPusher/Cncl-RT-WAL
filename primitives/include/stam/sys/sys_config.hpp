#pragma once

// 1) Пользовательский override (опционально).
// Подключится, если пользователь добавил файл в include-path.
#if defined(__has_include)
  #if __has_include("user_sys_config.hpp")
    #include "user_sys_config.hpp"
  #endif
#endif

// 2) Внутренние дефолты (если пользователь не определил).
#ifndef SYS_ENABLE_RT
  #define SYS_ENABLE_RT 1
#endif

#ifndef SYS_ASSUME_SINGLE_CORE
  // Для Cortex-M часто true, для SMP — false.
  #define SYS_ASSUME_SINGLE_CORE 0
#endif

#ifndef SYS_CACHELINE_BYTES
  // "Безопасный" дефолт для десктопа; на Cortex-M можно поставить 32 или 0 (если cache нет).
  #define SYS_CACHELINE_BYTES 64
#endif

#ifndef SYS_RB_ALIGNMENT
  // Выравнивание структур ринга: обычно cacheline.
  #define SYS_RB_ALIGNMENT SYS_CACHELINE_BYTES
#endif

#ifndef SYS_USE_STD_ATOMICS
  #define SYS_USE_STD_ATOMICS 1
#endif

#ifndef SYS_STRICT_RT
  // 1 = более жёсткие проверки/запреты (assert/annotations)
  #define SYS_STRICT_RT 1
#endif

// 3) Подключаем платформенные детали
#include "sys/sys_platform.hpp"
#include "sys/sys_compiler.hpp"
#include "sys/sys_arch.hpp"
#include "sys/sys_align.hpp"
#include "sys/sys_fence.hpp"
#include "sys/sys_rt.hpp"
