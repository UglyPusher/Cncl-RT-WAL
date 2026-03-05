# SYS_PORTABILITY.md
Version: v0.1 (portable config layer)

This document describes how to port and configure the ЫНЫ/RT utility layer across
compilers, CPUs, and RTOS/OS environments without modifying library source code.
Configuration is performed via **preprocessor defines** (CMake/toolchain) and an
optional **user_sys_config.hpp** override file.

---

## 0. Design goals (what “portable” means here)

1. **Same sources** build on x86/x64 and ARM (Cortex-M / Cortex-A).
2. RT/non-RT boundaries are explicit and testable (as far as the platform allows).
3. Memory ordering is correct by the **C++ memory model** by default.
4. Architecture-specific fences exist as an opt-in fallback (SoC/RTOS edge cases).
5. Cacheline/alignment tuning is isolated to configuration.

---

## 1. Configuration surfaces (in order of precedence)

1) **user_sys_config.hpp** (optional; user-provided, in include path)
2) **CMake target_compile_definitions** (or toolchain defines)
3) Library defaults (sys_config.hpp)

Recommended: per-platform toolchain/preset sets macro values; application can still
override via user_sys_config.hpp for special builds.

---

## 2. Core knobs (macros)

### 2.1 Semantics / project policy

| Macro | Type | Default | Meaning |
|------|------|---------|---------|
| `SYS_ENABLE_RT` | 0/1 | 1 | Enables RT-related assumptions/contracts. |
| `SYS_STRICT_RT` | 0/1 | 1 | Enables stricter assertions/markers for RT. |
| `SYS_USE_STD_ATOMICS` | 0/1 | 1 | Use `std::atomic` and `atomic_thread_fence`. |

### 2.2 Platform / hardware

| Macro | Type | Default | Meaning |
|------|------|---------|---------|
| `SYS_ASSUME_SINGLE_CORE` | 0/1 | 0 | If 1: single-core assumptions allowed (still may need IRQ barriers). |
| `SYS_CACHELINE_BYTES` | int | 64 | Cache line size. Set 32/64, or 0 if unknown/no-cache. |
| `SYS_RB_ALIGNMENT` | int | `SYS_CACHELINE_BYTES` | Alignment for ring/rw-hot structs. |
| `SYS_HAS_IN_RT_CONTEXT` | 0/1 | 0 | If 1: user supplies `bool sys_in_rt_context() noexcept;`. |

### 2.3 Fence mode (optional extension)
If you add a macro (recommended) to control CPU fences:

| Macro | Type | Suggested default | Meaning |
|------|------|-------------------|---------|
| `SYS_ENABLE_CPU_FENCES` | 0/1 | 0 | If 1: enable explicit CPU fences (DMB/MFENCE). |

By default, the library should rely on **C++ fences** only.

---

## 3. Memory ordering policy (portable baseline)

### 3.1 Baseline (recommended everywhere)
Use `std::atomic` operations with `memory_order_release/acquire` or
`atomic_thread_fence(release/acquire)` in the correct places.

**Rationale**: This is the only portable contract that compilers and CPUs must respect.

### 3.2 When to enable explicit CPU fences
Enable `SYS_ENABLE_CPU_FENCES=1` only if:
- You are on bare-metal / unusual toolchain and you cannot trust `std::atomic` mapping.
- You are interacting with device memory / MMIO or DMA visibility rules (then you likely
  need DMB/DSB with platform-specific semantics anyway).
- You have verified a platform bug / ABI constraint.

---

## 4. Alignment and false sharing

### 4.1 Defaults
- Desktop/server: `SYS_CACHELINE_BYTES=64`
- Many Cortex-M: no data cache → you *can* set 0 or keep 32 for padding discipline.
- Cortex-A: typically 64.

### 4.2 Rules of thumb
- Put frequently written independent atomics (e.g., head/tail) on different cache lines:
  use `alignas(SYS_CACHELINE_BYTES)` and explicit padding.
- Alignment (`alignas(N)`) affects *address* alignment; it does not automatically “consume
  N bytes”, but padding can increase structure size due to layout rules.

---

## 5. RT boundary hooks

### 5.1 Optional `sys_in_rt_context()`
If your environment can tell whether you are in an RT/ISR context, implement:

```cpp
// user code
bool sys_in_rt_context() noexcept;