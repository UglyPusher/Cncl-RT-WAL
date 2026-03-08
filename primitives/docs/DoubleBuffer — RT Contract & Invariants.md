# DoubleBuffer — RT Contract & Invariants

## 0. Scope

`DoubleBuffer<T>` is a ping-pong (double) buffer for transferring state snapshots between two execution contexts:

- **Producer (writer)** — single thread / RT context, write-only.
- **Consumer (reader)** — single thread / non-RT or RT-compatible context, read-only.

Designed for:

- delivering the latest state
- with minimal latency
- without waits, locks, or syscalls
- with acceptable loss of intermediate updates

**This is not a queue and not a log.**

---

## 0.1 UP Init Contract

Initialization and wiring are defined as **UP init**:

- all `writer()` / `reader()` issuance and bind steps are executed in a single-thread bootstrap phase;
- scheduler is not running yet;
- parallel/multi-core init for the same primitive instance is not allowed.

Handle issuance guards in code rely on this contract.

---

## 1. Semantic Model

`DoubleBuffer` implements a **last-writer-wins snapshot** model:

- The producer publishes a complete new state atomically.
- The consumer sees the last published state, provided the system ensures `read()` and `write()` do not overlap in time (see Section 4.1).
- All intermediate states may be lost.

Formally:

```
state(t_consumer) = last state published before read(),
                    assuming no overlap of read() and write()
```

---

## 2. Compile-Time Invariants

`T` must satisfy:

```cpp
std::is_trivially_copyable_v<T>
```

Consequences:

- No constructors or destructors.
- No hidden allocations.
- Copy is bounded and deterministic.
- `memcpy`-like behavior is permitted.

The size of `T` is fixed and known at compile time.

---

## 3. Runtime Invariants

**Notation:**

| Symbol | Meaning |
|---|---|
| `buffers[0]`, `buffers[1]` | Two data slots |
| `published` | Index of the published slot (0 or 1) |

**Invariants:**

**Single-writer rule** — `published` is modified only by the producer; the consumer only reads it.

**Slot ownership** — the producer writes only to the *non-published* slot; the consumer reads only the *published* slot.

**Atomic publication** — publishing a new state is a single `atomic store(release)`.

**No partial visibility** — the consumer never observes a partially written `T`, provided `read()` and `write()` do not overlap in time (see Section 4.1). If overlap occurs, the slot ownership invariant may be violated and a torn read of `T` is possible.

---

## 4. Threading Model (Preconditions)

- Exactly **one** producer per `DoubleBuffer` instance.
- Exactly **one** consumer per `DoubleBuffer` instance.
- The producer is **not re-entrant**: nested IRQ/NMI calling `write()` is forbidden.
- The consumer is **not re-entrant**.

Violating these preconditions results in **undefined behavior**.

### 4.1 Preemption / SMP Safety

This component does **not** guarantee torn-free snapshots in general SMP or preemptive systems.

**Mechanism of failure.** The consumer executes `read()` in two steps: (1) load `published` index, (2) copy `buffers[idx]`. If the consumer is preempted between these two steps and the producer completes **two** `write()` calls during that window, the producer recycles the slot the consumer is about to copy — slot ownership is violated and the consumer observes a torn `T`.

This scenario requires no SMP: it reproduces on a single CPU under a preemptive scheduler.

**No-torn guarantee holds when** `read()` and `write()` are guaranteed not to overlap in time across their full execution, including the copy of `T`. Sufficient conditions (any one of):

- Scheduling policy ensures the consumer is never preempted for longer than one write period.
- Application-level rate contract: producer fires at most once per consumer activation.
- Consumer runs in a non-preemptible region (e.g., IRQ context with masking).
- Explicit IRQ masking around `read()`.

**The caller is responsible** for establishing one of the above conditions. The component provides no runtime enforcement.

---

## 5. Memory Ordering / Happens-Before

**Publication rule (producer → consumer):**

```cpp
// Producer
buffers[next] = value;
published.store(next, memory_order_release);

// Consumer
idx = published.load(memory_order_acquire);
out = buffers[idx];
```

**Guarantee:** if the consumer observes `published == i`, then all writes to `buffers[i]` performed by the producer before the `store(release)` happen-before the consumer's read.

No additional fences are required.

---

## 6. Progress Guarantees

**Producer — `write()`:**

- Wait-free, O(1).
- No loops, CAS, waits, or locks.
- Execution time is strictly bounded.

**Consumer — `read()`:**

- O(1), always completes in bounded time.
- Never blocked by producer actions.

---

## 7. RT Contract (Producer Side)

`write()` satisfies hard-RT requirements.

**Guarantees:**

- Bounded execution time.
- No locks.
- No syscalls.
- No dynamic memory.
- No branching on consumer state.

**Explicit non-guarantees:**

- Delivery of every update is not guaranteed.
- The number of skipped states is not tracked.

---

## 8. Non-RT / Mixed-RT Usage (Consumer Side)

`read()` may be called from:

- non-RT context
- soft-RT context
- hard-RT context (if copying `T` fits within the time budget)

The consumer has no effect on the producer.

---

## 9. Initial State

Before the first `write()`, `read()` returns zero-initialized `T` (consequence of value-initialization of `DoubleBufferCore`).

This has **defined behavior** at the C++ level but **unspecified semantics** at the application level: the caller cannot distinguish "no data published yet" from "a valid snapshot with all-zero value."

If detection of the initial unpublished state is required, add an explicit flag or version counter on top.

---

## 10. Error Model

The component has no runtime errors:

- No error return values.
- No exceptions.
- No error codes.

**Loss of intermediate states is architectural, not an error.**

---

## 11. Misuse Scenarios (Forbidden)

The following are forbidden and result in undefined behavior:

- More than one producer or consumer on the same instance.
- Using `T` with non-trivial lifetime (constructors, destructors, allocations).
- Violating the slot ownership protocol defined in Section 3.
- Using `volatile` instead of atomics.
- Using `DoubleBuffer` as a queue or log.
- Attempting to detect skipped updates without an additional sequence mechanism.
- Calling `read()` from a context where preemption by the producer is possible and the rate contract (Section 4.1) is not established.

---

## 12. When to Use DoubleBuffer

Use only when:

- Data represents **state**, not events.
- Minimal latency is a priority.
- Loss of intermediate updates is acceptable.
- The system guarantees non-overlapping `read()` / `write()` execution (see Section 4.1).
- The RT path must be as short as possible.

**Typical applications:**

- Current setpoints
- Aggregated telemetry
- Sensor state
- Runtime configuration snapshot

---

## 13. Relation to SPSCRing

| Property | DoubleBuffer | SPSCRing |
|---|---|---|
| Model | Snapshot | Stream |
| Data loss | Always possible | On overflow |
| Latency | ≤ 1 publish | ≤ capacity |
| Backpressure | None | Limited |
| RT path | Minimal | Minimal, but longer |

---

## 14. Summary

`DoubleBuffer<T>` is a minimal RT primitive for state transfer. It provides wait-free publication and bounded-time reads, but **does not internally enforce torn-free snapshots** under preemption or SMP. Integrity of reads depends on a system-level guarantee that `read()` and `write()` do not overlap in time (Section 4.1).

It is not a queue and is not intended for logging.
