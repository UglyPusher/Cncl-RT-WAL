# exec/primitives

Low-level RT primitives for inter-thread data transfer.

All components are designed for strict real-time requirements:
bounded deterministic operations, wait-free producer paths,
no dynamic memory,
no exceptions,
no locks.

Snapshot API unification (v1):
- canonical write path: `write(const T&)`
- canonical read path: `try_read(T&) -> bool`
- legacy aliases (`publish()`, `read()`) remain for backward compatibility.
- generic snapshot concepts: `SnapshotWriter<W, T>`, `SnapshotReader<R, T>`
  in `include/stam/primitives/snapshot_concepts.hpp`.

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

### DoubleBufferSeqLock

SPSC snapshot buffer (latest-wins), SMP-safe. SeqLock variant of `DoubleBuffer`.
`write()` always succeeds (wait-free). `read()` retries until a stable snapshot is
obtained (lock-free). Before the first `write()`, `read()` returns zero-initialized T.

| File | Documentation |
|---|---|
| `dbl_buffer_seqlock.hpp` | [`docs/DoubleBufferSeqLock - RT Contract & Invariants.md`](docs/DoubleBufferSeqLock%20-%20RT%20Contract%20%26%20Invariants.md) |

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

### Mailbox2SlotSmp

SPSC snapshot mailbox (latest-wins), SMP-safe. Uses 2 slots + per-slot sequence counters.
`try_read()` returns `false` if no data yet or a concurrent write is detected (single-shot).
Writer must complete 2 full publish cycles to cause a reader collision — low false-return
rate in RT polling loops.

| File | Documentation |
|---|---|
| `mailbox2slot_smp.hpp` | [`docs/Mailbox2SlotSmp - RT Contract & Invariants.md`](docs/Mailbox2SlotSmp%20-%20RT%20Contract%20%26%20Invariants.md) |

---

### SPMCSnapshot

SPMC snapshot channel (latest-wins, wait-free). Transfers the latest
published state from one writer to up to N concurrent readers.
`try_read()` returns `false` only before the first `publish()`;
after that always returns `true`.

> **UP-only / Condition B SMP.** `try_read()` uses `sys_preemption_disable/enable`
> to protect the load-published → set-busy_mask window. This is correct on a
> single core and on SMP where the scheduler guarantees temporal separation
> (writer finishes publishing before readers start a new tick). For general SMP
> use `SPMCSnapshotSmp`.

| File | Documentation |
|---|---|
| `spmc_snapshot.hpp` | [`docs/SPMCSnapshot — RT Contract & Invariants.md`](../docs/SPMCSnapshot%20—%20RT%20Contract%20%26%20Invariants.md) |

---

### SPMCSnapshotSmp

SPMC snapshot channel (latest-wins), SMP-safe. Uses `fetch_or + re-verify + refcnt`
protocol instead of `preemption_disable`. `try_read()` is wait-free per invocation;
returns `false` before first publish or when publication changed during claim.

| File | Documentation |
|---|---|
| `spmc_snapshot_smp.hpp` | [`docs/SPMCSnapshotSmp - RT Contract & Invariants.md`](docs/SPMCSnapshotSmp%20-%20RT%20Contract%20%26%20Invariants.md) |

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
| `DoubleBufferSeqLock` | Snapshot / latest-wins | Intermediate states are lost | No (read retries) | Always succeeds |
| `Mailbox2Slot` | Snapshot / latest-wins | Intermediate states are lost | No (claim-verify) | Always succeeds (overwrite inactive slot) |
| `Mailbox2SlotSmp` | Snapshot / latest-wins | Intermediate states are lost | No (single-shot) | Always succeeds |
| `SPMCSnapshot` | Snapshot / latest-wins | Intermediate states are lost | No | Always succeeds |
| `SPMCSnapshotSmp` | Snapshot / latest-wins | Intermediate states are lost | No (single-shot) | Always succeeds |
| `SPSCRing` | Queue / FIFO | No (if space is available) | No | Returns `false` |

---

## SMP Safety Matrix

| Primitive | UP-safe | SMP-safe | Notes |
|---|---|---|---|
| `DoubleBuffer` | ✓ | ✗ | Uses `preemption_disable`; SMP variant: `DoubleBufferSeqLock` |
| `DoubleBufferSeqLock` | ✓ | ✓ | SeqLock; writer wait-free, reader lock-free |
| `Mailbox2Slot` | ✓ | ✗ | Uses `preemption_disable`; SMP variant: `Mailbox2SlotSmp` |
| `Mailbox2SlotSmp` | ✓ | ✓ | 2-slot + per-slot seq; writer wait-free, reader wait-free per invocation |
| `SPMCSnapshot` | ✓ | ✗ / cond. | UP + Condition B SMP only; general SMP variant: `SPMCSnapshotSmp` |
| `SPMCSnapshotSmp` | ✓ | ✓ | fetch_or + refcnt protocol; both sides wait-free per invocation |
| `SPSCRing` | ✓ | ✓ | Uses `acquire`/`release` atomics; no preemption guard needed |

---

## Common Requirements For All Primitives

- Roles are fixed by API: producer side writes, consumer side reads.
- `T` must satisfy `std::is_trivially_copyable_v<T> == true`.
- Operations are **non-reentrant** on one role instance (no nested IRQ/NMI on the same role).
- **Single producer** means temporal exclusivity across all cores:
  concurrent producer calls from different CPUs are a data race and UB.
- No dynamic memory, no exceptions, no system calls.
- All atomics used in the primitive must be lock-free on the target platform.

### Topology-Specific Requirements

- **SPSC primitives** (`DoubleBuffer`, `DoubleBufferSeqLock`, `Mailbox2Slot`, `Mailbox2SlotSmp`, `SPSCRing`):
  exactly one producer and exactly one consumer.
- **SPMC primitives** (`SPMCSnapshot`, `SPMCSnapshotSmp`):
  exactly one producer and up to `N` concurrent consumers (as defined by the template parameter).

---

## SMP And Multi-Core Limitations

### DoubleBuffer — UP-only

Uses no explicit locking. Snapshot integrity is guaranteed only when there is no
**temporal overlap** between `read()` and `write()`. On SMP, the producer on one
core can recycle the slot being copied by the consumer on another core.
**Use `DoubleBufferSeqLock` for SMP.**

### Mailbox2Slot — UP-only

The claim-verify protocol uses `sys_preemption_disable/enable` to protect
critical sections. Correct **on a single core only** (bare-metal, RTOS with
one active core). On SMP, disabling preemption on the reader's core does not
prevent the writer (on another core) from observing a stale `lock_state` and
writing to the slot the reader is about to claim.
**Use `Mailbox2SlotSmp` for SMP.**

### SPMCSnapshot — UP-only / Condition B SMP

Uses `sys_preemption_disable/enable` to protect the
load-published → set-busy_mask window. Correct on a single core (Condition A)
and on SMP where the scheduler guarantees temporal separation between
writer and readers (Condition B). Without Condition B, physical parallelism
between cores creates a torn-read race.
**Use `SPMCSnapshotSmp` for general SMP.**

### SPSCRing — SMP-safe

Does not use a preemption guard. Uses `memory_order_acquire/release` on
`head_` and `tail_`. Correct on SMP as long as the SPSC contract is respected
(exactly one producer and one consumer across all cores).

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
primitives/tests/
    crc32_rt_test.cpp
    dbl_buffer_test.cpp
    mailbox2slot_test.cpp
    spsc_ring_test.cpp
```

Run via CTest:

```sh
ctest -L primitives
```
