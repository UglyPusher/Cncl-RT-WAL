# Memory Footprint — Primitives (RAM/Flash Estimation)

This note provides quick, order-of-magnitude memory estimates for STAM primitives.

For *exact* numbers, prefer compiling for your target and checking `sizeof(...)`
and the linker map: C++ ABI, `std::atomic` implementation, and alignment rules
are toolchain/arch dependent.

---

## 1. Inputs that dominate footprint

- `T` size: `sizeof(T)` (payload)
- `SYS_CACHELINE_BYTES` (alignment / padding policy)
- Template parameters (`Capacity`, `N` readers, etc.)
- `sizeof(std::atomic<...>)` on the target (especially important on MCUs)

**About `SYS_CACHELINE_BYTES` on MCUs.**
On MCUs without data cache, setting `SYS_CACHELINE_BYTES` to a non-zero value
increases RAM usage (padding) without bringing cacheline false-sharing benefits.
If you still set it (e.g. `32`) for structural/layout consistency across builds,
factor the padding into RAM budgets.

---

## 2. Notation (rough sizing)

Let:

- `C = SYS_CACHELINE_BYTES`
- `A = max_alignment` of the core members (often `C` when `alignas(C)` is used)
- `round_up(x, a)` = smallest multiple of `a` ≥ `x`

When a type is declared `alignas(C)`, its `sizeof(...)` is typically rounded up
to a multiple of `C` (object size must be a multiple of its alignment).

---

## 3. RAM formulas (core data only)

### DoubleBuffer<T>

Core has:
- 2 slots (ping/pong)
- 1 published index (`atomic<uint32_t>`)

Rough:
- `slot_bytes ≈ (C ? round_up(sizeof(T), C) : sizeof(T))`
- `core_bytes ≈ 2 * slot_bytes + (C ? C : sizeof(std::atomic<uint32_t>))`

Wrapper (`DoubleBuffer<T>`) adds 2 issue-guards (`atomic<bool>`) and padding.

Example (`sizeof(T)=128`, `C=32`):
- `slot_bytes = 128`
- `core_bytes ≈ 256 + 32 = 288`
- `DoubleBuffer<T> total ≈ 320` bytes (typical, depends on `atomic<bool>` size/padding)

### DoubleBufferSeqLock<T>

Core has:
- 1 seq counter line (`alignas(C) atomic<uint32_t>`)
- 1 payload slot (`alignas(C) T`)

Rough:
- `seq_line_bytes ≈ (C ? C : sizeof(std::atomic<uint32_t>))`
- `slot_bytes ≈ (C ? round_up(sizeof(T), C) : sizeof(T))` (but in this implementation `C > 0` is required)
- `core_bytes ≈ seq_line_bytes + slot_bytes`

Example (`sizeof(T)=128`, `C=32`): `core_bytes ≈ 32 + 128 = 160`.

### Mailbox2Slot<T> (UP-only)

Core has:
- 2 slots (`alignas(C)`)
- 2 control cachelines: `pub_state` and `lock_state` (each padded to 1 cacheline)

Rough (this implementation requires `C > 0`):
- `slot_bytes ≈ round_up(sizeof(T), C)`
- `core_bytes ≈ 2 * slot_bytes + 2 * C`

Example (`sizeof(T)=128`, `C=32`): `core_bytes ≈ 256 + 64 = 320`.

### Mailbox2SlotSmp<T>

Core has:
- 2 slots
- 2 per-slot seq lines (each one cacheline)
- 1 control line (one cacheline)

Rough (requires `C > 0`):
- `slot_bytes ≈ round_up(sizeof(T), C)`
- `core_bytes ≈ 2 * slot_bytes + 3 * C`

Example (`sizeof(T)=128`, `C=32`): `core_bytes ≈ 256 + 96 = 352`.

### SPMCSnapshot<T, N> (UP-only / Condition-B SMP)

Let `K = N + 2`.

Core has:
- `K` slots
- 1 control line (one cacheline)
- `K` refcnt entries, aligned as a block (`alignas(C) atomic<uint8_t>[K]`)

Rough (requires `C > 0`):
- `slot_bytes ≈ round_up(sizeof(T), C)`
- `refcnt_bytes ≈ round_up(K * sizeof(std::atomic<uint8_t>), C)` (often rounded/padded by the compiler)
- `core_bytes ≈ K * slot_bytes + C + refcnt_bytes`

### SPMCSnapshotSmp<T, N>

Let `K = N + 2`.

Adds per-slot sequence lines:
- `K` seq lines (each exactly one cacheline in this implementation)

Rough (requires `C > 0`):
- `slot_bytes ≈ round_up(sizeof(T), C)`
- `refcnt_bytes ≈ round_up(K * sizeof(std::atomic<uint8_t>), C)`
- `core_bytes ≈ K * slot_bytes + K * C + C + refcnt_bytes`

Example (`sizeof(T)=128`, `C=32`, `N=4` → `K=6`):
- `slots ≈ 6 * 128 = 768`
- `seq ≈ 6 * 32 = 192`
- `ctrl ≈ 32`
- `refcnt ≈ round_up(6 * sizeof(atomic<uint8_t>), 32)` → typically `32`
- `core ≈ 768 + 192 + 32 + 32 = 1024`

### SPSCRing<T, Capacity>

Core has:
- 2 indices (`head_`, `tail_`) each `alignas(C)`
- explicit padding `pad_[C]`
- buffer `alignas(C) T[Capacity]`

Rough (requires `C > 0`):
- `buffer_bytes ≈ round_up(sizeof(T), C) * Capacity` (since `alignas(C)` on array enforces alignment; element size is `sizeof(T)`)
- `core_bytes ≈ C + C + C + buffer_bytes` (two atomics on separate cachelines + explicit pad + buffer)

In practice the ring is dominated by `Capacity * sizeof(T)`.

### SPSCRingDropOldest<T, Capacity>

Same core layout as `SPSCRing<T, Capacity>` (dominant term is still the buffer).

---

## 4. Flash (very approximate)

Flash cost depends on:
- whether payload copies inline or call `memcpy`
- `std::atomic` lowering (LL/SC vs libatomic calls)
- optimization level (`-O2` vs `-Os`) and inlining decisions

Rule of thumb:
- For snapshot primitives with `sizeof(T)=128`, the hot path is usually
  “a few atomic ops + one 128-byte copy”. The copy may be a call into the
  runtime (`__aeabi_memcpy`) or a short loop, so the per-call-site flash
  cost ranges from *tens of bytes* (call) to *hundreds of bytes* (inlined loop).

---

## 5. How to get exact numbers (recommended)

Add a small build-only TU for your target:

```cpp
#include <cstdio>
#include "stam/primitives/dbl_buffer.hpp"
#include "stam/primitives/mailbox2slot_smp.hpp"

struct Blob128 { unsigned char b[128]; };

int main() {
    std::printf("DoubleBuffer<Blob128> = %zu\n", sizeof(stam::primitives::DoubleBuffer<Blob128>));
}
```

Then inspect:
- `sizeof(...)` output
- linker `.map` / `size` for flash/ram totals

