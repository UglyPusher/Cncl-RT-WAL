# exec/primitives

Low-level RT primitives for inter-thread data transfer.

All components are designed for strict real-time requirements:
bounded deterministic operations, wait-free producer paths,
no dynamic memory,
no exceptions,
no locks.

---

## Components

### DoubleBuffer

Ping-pong snapshot buffer (latest-wins). Transfers the latest
published state from one writer to one reader.
`write()` always succeeds, `read()` always returns a value.

> **Note:** snapshot integrity is guaranteed only when there is no
time overlap between `read()` and `write()`. Enforcing non-overlap is a
system-level contract and is the caller's responsibility (see contract §4.1).

| File | Documentation |
|---|---|
| `dbl_buffer.hpp` | [`docs/contracts/DoubleBuffer - RT Contract & Invariants.md`](../../../docs/contracts/DoubleBuffer%20—%20RT%20Contract%20&%20Invariants.md) |

---

### Mailbox2Slot

SPSC snapshot mailbox with latest-wins semantics and a
claim-verify protocol. Unlike `DoubleBuffer`, `try_read()` may
return `false` during a publication race, and the reader stays on the
previous (sticky) state. No retry: miss = next tick.

| File | Documentation |
|---|---|
| `mailbox2slot.hpp` | [`docs/contracts/Mailbox2Slot_v1.3 - RT Contract & Invariants.md`](../../../docs/contracts/Mailbox2Slot_v1.3%20—%20RT%20Contract%20&%20Invariants.md) |

---

### SPSCRing

SPSC Ring (FIFO Event Channel)

Single-Producer Single-Consumer lock-free ring buffer (FIFO).

Progress guarantees

Producer `push()` - wait-free O(1)
The operation finishes in a bounded number of steps, with no waiting and no retries.
If the queue is full, `push()` returns `false`.

Consumer `pop()` - wait-free try-pop O(1)
The operation finishes in a bounded number of steps and returns either `true` or `false`.
`false` means the queue is empty.

Delivery semantics

All successfully written elements are delivered in FIFO order.

Writing an element is not guaranteed: if the queue is full, `push()` returns `false`.

The queue never blocks the producer and does not overwrite data.

Capacity

Capacity must be a power of two.
Usable capacity: `usable = Capacity - 1`

| File | Documentation |
|---|---|
| `spsc_ring.hpp` | [`docs/contracts/SPSCRing - RT Contract & Invariants.md`](../../../docs/contracts/SPSCRing%20—%20RT%20Contract%20&%20Invariants.md) |

---

### crc32_rt

CRC32C (Castagnoli) with incremental and one-shot interfaces.
The lookup table is generated at compile time (`constexpr`),
with no runtime initialization. Algorithm correctness is validated by a `static_assert`
using the canonical test vector `"123456789"` -> `0xE3069283`.

| File | Documentation |
|---|---|
| `crc32_rt.hpp` | *(embedded in header)* |

---

## Semantic Comparison

| Primitive | Semantics | Data Loss | Blocking | `push`/`write` when full |
|---|---|---|---|---|
| `DoubleBuffer` | Snapshot / latest-wins | Intermediate states are lost | No | Always succeeds (overwrite inactive slot) |
| `Mailbox2Slot` | Snapshot / latest-wins | Intermediate states are lost | No (claim-verify) | Always succeeds (overwrite inactive slot) |
| `SPSCRing` | Queue / FIFO | No (if space is available) | No | Returns `false` |

---

## Common Requirements For All Primitives

- Exactly **one producer** and **one consumer** (SPSC).
- `T` must satisfy `std::is_trivially_copyable_v<T> == true`.
- Operations are **non-reentrant** (no nested IRQ/NMI on the same core).
- **"Exactly one producer"** means temporal exclusivity across all cores:
  concurrent `write()` calls from different CPUs are a data race and UB.
- No dynamic memory, no exceptions, no system calls.
- `std::atomic` with `is_always_lock_free == true` on the target platform.

---

## SMP And Multi-Core Limitations

### DoubleBuffer

Snapshot integrity is guaranteed only when there is no
**temporal overlap** between `read()` and `write()`.
On SMP systems where producer and consumer run on different cores,
this contract must be enforced by the scheduler
or overall system architecture - the primitive itself does not enforce it
(more details: contract §4.1).

### Mailbox2Slot

The claim-verify protocol uses `sys_preemption_disable/enable` to
protect critical sections. This guarantees correctness
**in a single-core context** (bare-metal, RTOS with one active core).

> **SMP warning.** On true multi-processor systems, disabling
> preemption on one core does **not** prevent simultaneous access
> by a writer running on another core to the same slot. In this case,
> torn reads are possible unless the platform provides cache-line
> coherence guarantees (x86-64, ARM Cortex-A with DSB/ISB) and `sizeof(T)`
> does not exceed the atomically coherent data granularity.
>
> Before using this on an SMP platform, ensure that the system
> architecture prevents simultaneous producer/consumer access
> to the same primitive, or that platform coherence guarantees
> are sufficient for the specific `T`.

### SPSCRing

Does not use a preemption guard. Correct on SMP as long as the
SPSC contract is respected (exactly one producer and one consumer in time
across all cores).

---

## Component Structure

All primitives follow the same pattern:

```
<Name>Core<T>       - shared-state carrier (public fields,
                      explicit and verifiable layout/invariants)
<Name>Writer<T>     - producer view (write-only)
<Name>Reader<T>     - consumer view (read-only)
<Name><T>           - convenience wrapper (creates Core, provides Writer/Reader)
```

---

## Tests

```
tests/primitives/
    crc32_rt_test.cpp
    dbl_buffer_test.cpp
    mailbox2slot_test.cpp
    spsc_ring_test.cpp
```

Run via CTest:

```sh
ctest -L primitives
```
