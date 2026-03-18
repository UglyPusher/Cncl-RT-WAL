# Sys — Portability Contract

## EN Summary (short)

`stam::sys` is the portability layer used by STAM RT primitives. A platform port must provide a small set of configuration macros and a few platform hooks (most importantly preemption control) with strict RT properties (bounded, no allocations, no blocking). Misconfiguration can silently break correctness assumptions, especially around UP vs SMP: disabling preemption only prevents same-core preemption and does not make UP-only primitives SMP-safe.

---

## 0. Назначение `stam::sys`

`stam::sys` — это portability layer, который:

- подключает пользовательские overrides (`user_sys_config.hpp`) и задает разумные дефолты;
- определяет параметры платформы (OS/RTOS hints, endian, наличие cache/threads);
- предоставляет небольшой набор утилит, используемых RT-примитивами:
  - cacheline alignment/padding;
  - preemption control hooks (критические секции);
  - fences (compiler/atomic/CPU);
  - выбор lock-free ширины битмасок (`signal_mask_t`);
  - RT-context detection и RT-safe assert.

Важно: `stam::sys` не является синхронизационным фреймворком. Он лишь дает примитивам то, что они ожидают от платформы.

### 0.1 Что `sys` НЕ гарантирует

- `preemption_disable()` **не делает код SMP-safe**. Он предотвращает только вытеснение/IRQ на *том же* core (в зависимости от вашей реализации). На SMP другой core продолжает выполняться физически параллельно.
- Дефолтные значения макросов подходят для “hosted dev/test”, но **не** обязаны быть корректными для вашей RT/embedded платформы.

---

## 1. Входная точка конфигурации

### `sys/sys_config.hpp`

Логика конфигурации:

1. **User override** (опционально): если в include-path присутствует `user_sys_config.hpp`, он автоматически подключится через `__has_include`.
2. **Library defaults**: макросы задаются только если пользователь не определил их ранее.
3. **Platform headers**: подключаются остальные заголовки portability layer.

Файл: `primitives/include/stam/sys/sys_config.hpp`.

---

## 2. Файлы и точки расширения (что за что отвечает)

Ниже перечислены ключевые заголовки `primitives/include/stam/sys/*` и то, что пользователь порта обычно настраивает/реализует.

### `sys_platform.hpp` (platform/OS hints)

- OS detection: `SYS_OS_LINUX`, `SYS_OS_WINDOWS`, `SYS_OS_APPLE`, `SYS_OS_UNIX`.
- Bare-metal heuristic: `SYS_BARE_METAL`.
- Наличие RTOS/threads:
  - `SYS_HAS_RTOS` (override пользователем),
  - `SYS_HAS_THREADS` (по умолчанию 0 на bare-metal, 1 на hosted).
- Endianness: `SYS_BYTE_ORDER`, `SYS_IS_LITTLE_ENDIAN`, `SYS_IS_BIG_ENDIAN`.
- Cache hints: `SYS_HAS_DATA_CACHE`, `SYS_PAGE_SIZE` (hint).
- Assert knobs: `SYS_ENABLE_ASSERTS`.

Файл: `primitives/include/stam/sys/sys_platform.hpp`.

### `sys_topology.hpp` (UP/SMP)

- Топология выбирается макросами:
  - `STAM_SYSTEM_TOPOLOGY_UP`
  - `STAM_SYSTEM_TOPOLOGY_SMP`
- Если SMP не задан явно, действует compatibility-first default: `kSystemTopologyIsSmp == false`.

Файл: `primitives/include/stam/sys/sys_topology.hpp`.

### `sys_align.hpp` (cacheline alignment/padding)

- `SYS_CACHELINE_BYTES`.
  - В текущей реализации примитивов `SYS_CACHELINE_BYTES` **должен быть > 0**:
    многие структуры используют `static_assert(SYS_CACHELINE_BYTES > 0)`.
  - Даже на платформах без D-cache (например, Cortex-M3/M4 без D-cache) задавайте
    “дисциплинарное” значение (обычно 32 или 64), чтобы сохранять инварианты layout.
- `SYS_CACHELINE_ALIGN` / `SYS_RB_ALIGN`.
- `sys_pad<N>` для явного padding.

Файл: `primitives/include/stam/sys/sys_align.hpp`.

### `sys_preemption.hpp` (обязательные preemption hooks)

Платформа **обязана** предоставить реализации:

- `stam::sys::sys_preemption_disable_impl() noexcept;`
- `stam::sys::sys_preemption_enable_impl() noexcept;`

А `stam::sys::preemption_disable()` / `preemption_enable()` — это thin wrappers.

Файл: `primitives/include/stam/sys/sys_preemption.hpp`.

### `sys_rt.hpp` (RT-context hook, RT-safe assert)

- `SYS_HAS_IN_RT_CONTEXT` + user hook:
  - `bool sys_platform_in_rt_context() noexcept;` (если включено)
- `SYS_RT_ASSERT(x)`:
  - в strict режиме и debug: “halt CPU” loop (без I/O / stdlib abort),
  - иначе: компилируется в no-op.

Файл: `primitives/include/stam/sys/sys_rt.hpp`.

### `sys_fence.hpp` (barriers/fences)

- Compiler fence: `sys_fence_compiler()`.
- C++ atomic fences: `sys_fence_release/acquire/acq_rel/seq_cst()`.
- CPU fence: `sys_cpu_fence_full()` выбирает asm в зависимости от `SYS_ARCH_*` и `SYS_ASSUME_SINGLE_CORE`.

Файл: `primitives/include/stam/sys/sys_fence.hpp`.

### `sys_signal.hpp` (lock-free mask width)

- `signal_mask_t` выбирается как “самый широкий lock-free базовый тип” из `uint64_t/uint32_t/uint16_t/uint8_t`.
- `signal_mask_width = sizeof(signal_mask_t) * 8`.

Файл: `primitives/include/stam/sys/sys_signal.hpp`.

---

## 3. Обязательные требования порта (MUST)

### 3.1 Preemption control

Вы **должны** реализовать `sys_preemption_disable_impl()` / `sys_preemption_enable_impl()` так, чтобы:

- `noexcept`
- bounded O(1)
- no allocations
- no blocking
- корректная парность “disable/enable” (восстановление состояния)

#### Когда допустим no-op

No-op допустим только если вы можете доказать на уровне системы, что реэнтранса и вытеснения в тех местах, где примитивы вызывают `preemption_disable`, физически не бывает. Примеры:

- single-thread cooperative superloop без IRQ в контексте producer/consumer;
- строгое правило, что `push()/publish()/try_read()` не вызываются из ISR/NMI и нет scheduler preemption.

Если этого нет, no-op превращает UP-only протоколы в UB/потерю корректности.

### 3.2 Cacheline и D-cache

Если на платформе есть D-cache и возможна false sharing, вы **должны**:

- задать `SYS_CACHELINE_BYTES` реальным значением (часто 32/64/128),
- обеспечить корректность `SYS_CACHELINE_ALIGN` геометрии.

Если D-cache нет (многие Cortex-M), обычно:

- `SYS_HAS_DATA_CACHE=0`,
- `SYS_CACHELINE_BYTES` задается как 32/64 (дисциплина layout; см. требования выше).

Важно: cacheline настройки не должны ломать layout assumptions в примитивах (например, static_assert’ы на slot alignment).

### 3.3 UP/SMP топология

Вы **должны** явно зафиксировать топологию:

- Для SMP сборок определить `STAM_SYSTEM_TOPOLOGY_SMP`.
- Для UP сборок можно не определять ничего (дефолт UP), но лучше определить `STAM_SYSTEM_TOPOLOGY_UP` для ясности.

Согласуйте это с `SYS_ASSUME_SINGLE_CORE` (используется в `sys_cpu_fence_full()` как оптимизация/режим).

### 3.4 Lock-free атомики

Вы **должны** обеспечить, что используемые атомики реально lock-free на таргете. Примитивы часто защищаются `static_assert(std::atomic<...>::is_always_lock_free)`.

Если платформа не поддерживает lock-free атомики нужной ширины:

- либо уменьшайте конфигурацию (например, маска/счетчики меньшей ширины),
- либо используйте другие примитивы/варианты (например, SMP-safe версии),
- либо не используйте эти компоненты на данной платформе.

---

## 4. Корректность и ограничения (UP vs SMP)

### 4.1 Ключевой принцип

`preemption_disable()` предотвращает только “temporal overlap” на том же CPU (в зависимости от реализации). На SMP это **не** исключает физическую параллельность и не может заменить межъядерную синхронизацию.

Практическое следствие:

- Примитивы, корректность которых опирается на “закрытие узкого окна” через `preemption_disable`, остаются **UP-only**.
- Для SMP используйте специализированные варианты, где протокол структурно исключает гонки (SeqLock, refcnt/busy-mask re-verify, и т.п.).

### 4.2 Привязка к семействам примитивов (примерно)

- UP-only (типично): примитивы, которые используют `preemption_disable` как замену синхронизации между producer/consumer.
- SMP-safe: примитивы, где используются атомики/протоколы, рассчитанные на физическую параллельность (например, `*Smp` варианты, `SeqLock`).

Точные требования смотрите в соответствующих `primitives/docs/*Contract & Invariants.md`.

---

## 5. Рекомендуемые настройки (SHOULD)

### 5.1 `SYS_CACHELINE_BYTES`

- Hosted x86_64/ARM64: обычно 64 (дефолт ок).
- Cortex-M без D-cache: обычно 32 (или 64), чтобы проходили `static_assert` и сохранялась геометрия layout.
- Cortex-M7/M33 с D-cache (зависит от SoC): задайте фактический размер линии (часто 32).

### 5.2 `SYS_ASSUME_SINGLE_CORE`

- Включайте `SYS_ASSUME_SINGLE_CORE=1` только если:
  - сборка UP,
  - и вы уверены, что не будет запуска на SMP.

### 5.3 `SYS_HAS_THREADS`, `SYS_HAS_RTOS`

- На bare-metal без RTOS: `SYS_HAS_THREADS=0`, `SYS_HAS_RTOS=0`.
- На RTOS: `SYS_HAS_RTOS=1`, а `SYS_HAS_THREADS` по факту вашей среды.

### 5.4 `SYS_STRICT_RT`, `SYS_ENABLE_ASSERTS`

- `SYS_STRICT_RT=1` полезен для раннего выявления misuse в dev/debug.
- Для hard-RT релизов часто выключают asserts через `SYS_ENABLE_ASSERTS=0` и/или `NDEBUG`.

---

## 6. Примеры реализаций

### 6.1 Cortex-M (CMSIS)

```cpp
#include <cmsis_gcc.h>

namespace stam::sys {
void sys_preemption_disable_impl() noexcept { __disable_irq(); }
void sys_preemption_enable_impl()  noexcept { __enable_irq();  }
} // namespace stam::sys
```

### 6.2 RTOS (псевдокод)

```cpp
namespace stam::sys {
void sys_preemption_disable_impl() noexcept { rtos_scheduler_lock(); }
void sys_preemption_enable_impl()  noexcept { rtos_scheduler_unlock(); }
} // namespace stam::sys
```

### 6.3 Hosted tests/dev (no-op с предупреждением)

```cpp
namespace stam::sys {
void sys_preemption_disable_impl() noexcept {}
void sys_preemption_enable_impl()  noexcept {}
} // namespace stam::sys
```

Допустимо только если вы не используете UP-only протоколы “как SMP-safe”, и понимаете, что `preemption_disable` здесь ничего не защищает.

---

## 7. Porting checklist

1. Определен ли `user_sys_config.hpp` и попадает ли он в include-path?
2. Зафиксирована ли топология `STAM_SYSTEM_TOPOLOGY_UP/SMP`?
3. Согласованы ли `STAM_SYSTEM_TOPOLOGY_*` и `SYS_ASSUME_SINGLE_CORE`?
4. Реализованы ли `sys_preemption_disable_impl` / `sys_preemption_enable_impl` (и линковка реально их находит)?
5. Гарантированы ли свойства RT для preemption hooks (O(1), noalloc, noblock, noexcept)?
6. Корректно ли задан `SYS_CACHELINE_BYTES` (или осознанно 0)?
7. Корректно ли задан `SYS_HAS_DATA_CACHE`?
8. Проходят ли `static_assert(is_always_lock_free)` для атомиков, которые использует ваша конфигурация?
9. Устраивает ли вас выбор `signal_mask_t` и хватает ли `signal_mask_width` для SPMC протоколов?
10. Если включен `SYS_HAS_IN_RT_CONTEXT`, реализован ли `sys_platform_in_rt_context()`?
11. Явно ли решено, включены ли asserts (`SYS_ENABLE_ASSERTS`) в релизе?
12. Примитивы UP-only реально не используются в SMP режиме (или выполнены условия “temporal separation”, если это описано в их контракте)?
