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

## 1. Semantic Model

`DoubleBuffer` implements a **last-writer-wins snapshot** model:

- The producer publishes a complete new state atomically.
- The consumer always sees the last fully published state.
- All intermediate states may be lost.

Formally:

```
state(t_consumer) = last state published before read()
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

**No partial visibility** — the consumer never observes a partially written `T`.

---

## 4. Threading Model (Preconditions)

- Exactly **one** producer per `DoubleBuffer` instance.
- Exactly **one** consumer per `DoubleBuffer` instance.
- The producer is **not re-entrant**: nested IRQ/NMI calling `write()` is forbidden.
- The consumer is **not re-entrant**.

Violating these preconditions results in **undefined behavior**.

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

## 9. Error Model

The component has no runtime errors:

- No error return values.
- No exceptions.
- No error codes.

**Loss of intermediate states is architectural, not an error.**

---

## 10. Misuse Scenarios (Forbidden)

The following are forbidden and result in undefined behavior:

- More than one producer or consumer on the same instance.
- Using `T` with non-trivial lifetime (constructors, destructors, allocations).
- Violating the slot ownership protocol defined in Section 3.
- Using `volatile` instead of atomics.
- Using `DoubleBuffer` as a queue or log.
- Attempting to detect skipped updates without an additional sequence mechanism.

---

## 11. When to Use DoubleBuffer

Use only when:

- Data represents **state**, not events.
- Minimal latency is a priority.
- Loss of intermediate updates is acceptable.
- The RT path must be as short as possible.

**Typical applications:**

- Current setpoints
- Aggregated telemetry
- Sensor state
- Runtime configuration snapshot

---

## 12. Relation to SPSCRing

| Property | DoubleBuffer | SPSCRing |
|---|---|---|
| Model | Snapshot | Stream |
| Data loss | Always possible | On overflow |
| Latency | ≤ 1 publish | ≤ capacity |
| Backpressure | None | Limited |
| RT path | Minimal | Minimal, but longer |

---

## 13. Summary

`DoubleBuffer<T>` is a minimal RT primitive for state transfer with a strict, formally verifiable contract — no compromises on latency or determinism.

It is not a queue and is not intended for logging.