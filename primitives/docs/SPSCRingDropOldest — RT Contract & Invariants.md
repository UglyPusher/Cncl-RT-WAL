# SPSCRingDropOldest - RT Contract & Invariants

## 0. Scope

`SPSCRingDropOldest<T, Capacity>` is a **Single-Producer / Single-Consumer**
fixed-size ring buffer for passing elements of type `T` between two execution
contexts, with **drop-oldest** overflow semantics:

* **Producer**: one write thread/context (including hard-RT).
* **Consumer**: one read thread/context (typically non-RT).

The component is designed for **bounded, deterministic publication** from the
producer side:

* no blocking
* no syscalls
* **bounded capacity = Capacity - 1**
* **data loss on overflow (oldest item dropped)**

Overflow reporting:
* `push()` returns `false` iff the call had to drop (overwrite) exactly one
  oldest element to make room for the new one.
* `push()` returns `true` if the element was enqueued without dropping.

The queue guarantees FIFO delivery of all **retained elements**.
Loss can happen only when the ring is full.

---

## 0.1 UP Init Contract

Initialization and wiring are defined as **UP init**:

* all `writer()` / `reader()` issuance and bind steps are executed in a
  single-thread bootstrap phase;
* scheduler is not running yet;
* parallel/multi-core init for the same primitive instance is not allowed.

Handle issuance guards in code rely on this contract.

---

## 1. Compile-time invariants

1. `Capacity` is a power of two and `Capacity >= 2`:

   ```
   Capacity >= 2 && (Capacity & (Capacity - 1)) == 0
   ```

2. `T` satisfies `std::is_trivially_copyable_v<T>`:

   * no hidden allocations
   * no destructors
   * copy cost is **bounded and deterministic**

---

## 2. Runtime invariants

### Notation

* `head` - index of the next write position (**producer-owned**)
* `tail` - index of the next read position (**consumer-owned**, but may be
  advanced by producer on overflow)
* indices are in `[0, Capacity)`
* index updates are masked with `(Capacity - 1)`

### Invariants

1. **Single-writer rule**

   * `head_` is modified only by producer

2. **Slot ownership**

   * slot `buffer_[i]` is written only by producer
   * slot `buffer_[i]` is read only by consumer
   * ownership transfer correctness is provided by publication through `head_`

3. **Drop-oldest on overflow**

   Producer never overwrites a slot that could be concurrently read.
   When full, the producer advances `tail_` by one slot (drops oldest),
   then publishes the new item into the reserved empty slot.

4. **No read-of-unpublished rule**

   Consumer does not read unpublished slots:

   ```
   pop() == false  <=>  tail == head
   ```

### Usable capacity

Maximum number of elements retained in the queue:

```
Capacity - 1
```

One slot is always reserved to distinguish **empty/full**.

---

## 3. Threading model requirements (preconditions)

1. Exactly **one producer** and **one consumer** per `SPSCRingDropOldest` instance.
2. `push()` is called only from the producer context.
3. `pop()` is called only from the consumer context.
4. Producer must be non-reentrant (for example, nested IRQ/NMI must not call
   `push()` concurrently).
5. Violating these conditions leads to **undefined behavior** within the
   component contract.
6. Consumer must also be non-reentrant (nested IRQ/NMI must not call `pop()`
   concurrently).

### 3.1 Handle issuance contract

`SPSCRingDropOldest::writer()` and `SPSCRingDropOldest::reader()` are
runtime-guarded:

* `writer()` may be issued at most once per ring lifetime.
* `reader()` may be issued at most once per ring lifetime.
* Exceeding either limit triggers fail-fast (`assert` + `abort`).
* Issuance guards are atomic/CAS-based to keep fail-fast race-safe even if
  init code is accidentally run concurrently.

---

## 4. Memory ordering / happens-before semantics

### Publication rule (producer -> consumer)

Producer:
- computes `next_head`
- reads `tail_` (acquire) and checks full
- if full: tries to advance `tail_` once (CAS, release) to drop oldest (no retry loop)
- writes `buffer_[head]`
- `head_.store(next_head, release)`

Consumer:
- `head_.load(acquire)` and checks empty
- reads `buffer_[tail]`
- `tail_.store(next_tail, release)`

**Guarantee:**
if `pop()` observes updated `head_` (acquire), it **must** observe the
corresponding write to `buffer_`.

---

## 5. RT contract (producer side)

`push()` satisfies hard-RT requirements:

### Guarantees

* **Bounded execution time**: O(1), no wait loops, no CAS retries
* **No blocking / waiting**: no mutex/spin/condvar
* **No allocation**: no dynamic allocation
* **No syscalls / IO**
* **Bounded loss**: on overflow, exactly one oldest element is dropped

`push()` is wait-free for the producer under the single-producer precondition.

---

## 6. Non-RT contract (consumer side)

`pop()`:
* wait-free try-pop O(1)
* each call always finishes in a bounded number of steps
* success depends on data availability

---

## 7. Telemetry APIs

`empty()` and `full()`:

* **empty()/full() may use relaxed memory reads and do not establish
  happens-before edges. It is forbidden to use their return values for
  synchronization or safety decisions about publication/consumption.**
* **Telemetry only; not for synchronization**

They may return stale values.

---

## 8. Cache / layout invariants

Goal: **jitter reduction**, not correctness.

1. `head_` and `tail_` are placed on separate cache lines
   (`alignas(SYS_CACHELINE_BYTES)`)

2. There is padding between control fields and `buffer_`:

```
pad[SYS_CACHELINE_BYTES]
```

3. `buffer_` is cache-line aligned
   -> fixed array start geometry

This is a **non-functional performance invariant**.
