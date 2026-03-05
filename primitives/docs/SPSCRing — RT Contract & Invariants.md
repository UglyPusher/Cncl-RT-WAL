# SPSCRing - RT Contract & Invariants

## 0. Scope

`SPSCRing<T, Capacity>` is a **Single-Producer / Single-Consumer** fixed-size ring buffer for passing elements of type `T` between two execution contexts:

* **Producer**: one write thread/context (including hard-RT).
* **Consumer**: one read thread/context (typically non-RT).

The component is designed for **bounded, deterministic, lossy publication** from the producer side:

* no blocking
* no syscalls
* **bounded capacity = Capacity - 1**
* potential **data loss on overflow** (`push()` returns `false`)

The queue guarantees FIFO delivery of all **successfully enqueued elements**.
Loss can happen only when `push() == false`.

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
* `tail` - index of the next read position (**consumer-owned**)
* indices are in `[0, Capacity)`
* index updates are masked with `(Capacity - 1)`

### Invariants

1. **Single-writer rule**

   * `head_` is modified only by producer
   * `tail_` is modified only by consumer

2. **Slot ownership**

   * slot `buffer_[i]` is written only by producer
   * slot `buffer_[i]` is read only by consumer
   * ownership transfer correctness is provided by publication through `head_`

3. **No overwrite rule**

   Producer does not overwrite unread slots:

   ```
   push() == false  <=>  next_head == tail
   ```

4. **No read-of-unpublished rule**

   Consumer does not read unpublished slots:

   ```
   pop() == false  <=>  tail == head
   ```

### Usable capacity

Maximum number of elements in the queue:

```
Capacity - 1
```

One slot is always reserved to distinguish **empty/full**.

---

## 3. Threading model requirements (preconditions)

1. Exactly **one producer** and **one consumer** per `SPSCRing` instance.
2. `push()` is called only from the producer context.
3. `pop()` is called only from the consumer context.
4. Producer must be non-reentrant (for example, nested IRQ/NMI must not call `push()` concurrently).
5. Violating these conditions leads to **undefined behavior** within the component contract.
6. Consumer must also be non-reentrant (nested IRQ/NMI must not call `pop()` concurrently).

---

## 4. Memory ordering / happens-before semantics

### Publication rule (producer -> consumer)

Producer:
- computes `next_head`
- reads `tail_` (acquire) and checks full
- if full: returns `false` without modifying state
- writes `buffer_[head]`
- `head_.store(next_head, release)`

Consumer:
- `head_.load(acquire)` and checks empty
- reads `buffer_[tail]`
- `tail_.store(next_tail, release)`

**Guarantee:**
if `pop()` observes updated `head_` (acquire),
it **must** observe the corresponding write to `buffer_`.

---

### Consumption rule (consumer -> producer)

Consumer publishes slot release:

```
tail_.store(next_tail, std::memory_order_release)
```

Producer checks:

```
tail_.load(std::memory_order_acquire)
```

**Guarantee:**
observing updated `tail_` means the slot is safe to reuse.

---

### Linearization points

* **push** linearizes at `head_.store(release)`
* **pop** linearizes at `tail_.store(release)`

Owner-side reads of `head_` and `tail_` may use `memory_order_relaxed`,
as no cross-thread synchronization is required for owned indices.

---

## 5. RT contract (producer side)

`push()` satisfies hard-RT requirements:

### Guarantees

* **Bounded execution time**: O(1), no wait loops, no CAS retries
* **No blocking / waiting**: no mutex/spin/condvar
* **No allocation**: no dynamic allocation
* **No syscalls / IO**
* **Bounded failure**: returns `false` on overflow
* **No side effects on failure**

### Explicit non-guarantees

* consumer processing is **not guaranteed**
* data **may be lost** on overflow

`push()` is wait-free for the producer under the single-producer precondition.

---

## 6. Non-RT contract (consumer side)

`pop()`:
* wait-free try-pop O(1)
* each call always finishes in a bounded number of steps
* success depends on data availability
* can be used in RT, but is **not positioned** as hard-RT API
  due to possible downstream non-RT data processing

---

## 7. Telemetry APIs

`empty()` and `full()`:

* **empty()/full() may use relaxed memory reads and do not establish happens-before edges. It is forbidden to use their return values for synchronization or safety decisions about publication/consumption.**
* **Telemetry only; not for synchronization**

They may return stale values:

* `empty()` may be `true` when an element has already been published
* `full()` may be `false` when the queue is already full

`empty()/full()` may use relaxed reads and do not form happens-before; they must not be used for synchronization or publication-safety decisions.

---

## 8. Cache / layout invariants

Goal: **jitter reduction**, not correctness.

1. `head_` and `tail_` are placed on separate cache lines
   (`alignas(SYS_CACHELINE_BYTES)`)

2. There is padding between control fields and `buffer_`:

```
pad[SYS_CACHELINE_BYTES]
```

-> separation of **control / data**

3. `buffer_` is cache-line aligned
   -> fixed array start geometry

This is a **non-functional performance invariant**.

On cache-coherent architectures, this layout eliminates false sharing
between producer-written and consumer-written cache lines.

---

## 9. Error model

The only API-level failures are:

* `push() == false` -> queue is full
* `pop() == false` -> queue is empty

If `push()` returns false, neither `buffer_` nor `head_` is modified.
If `pop()` returns false, neither `tail_` nor the output value is modified.

Guarantees:

* **no partial writes**
* **no state corruption**
* **no side effects on false**
* **no exceptions (`noexcept`)**

---

## 10. Extension policy (non-RT only)

Extensions (for example, batch-drain):

* must be implemented **on top of** `SPSCRing`
* must not alter `push()` RT semantics
* must not add work to the **producer RT path**

The base primitive remains:

> **minimal, formally verifiable, RT-deterministic.**


## 11. Progress guarantees

This section formalizes **progress guarantees** for `push()` and `pop()` using the classical non-blocking model.

### 11.1 Producer progress

Under section 3 preconditions:

* there is **exactly one producer**
* `push()` is non-reentrant

operation:

```
push()
```

has property:

> **wait-free for producer**

This means:

* execution time is **strictly bounded by a constant**
* there are no:
  * retry loops
  * CAS conflicts
  * dependence on actions of other threads
* completion is guaranteed in a **finite number of steps**,
  regardless of consumer state (including consumer stop).

Consequence:

* `push()` satisfies **hard real-time bounded progress**.

---

### 11.2 Consumer progress

Under section 3 preconditions:

* there is **exactly one consumer**
* `pop()` is non-reentrant

operation:

```
pop()
```

has property:

> `pop()` is wait-free as a try-pop operation: every call finishes in a bounded number of steps and returns either success or empty.

This means:

* system-level progress is guaranteed:
  * either the current `pop()` call finishes with success/failure,
  * or progress happens through future `pop()` or `push()` calls
* there is no:
  * deadlock
  * waiting on mutex/spinlock release
  * dependence on OS scheduler decisions.

`pop()` also has:

* **bounded execution time**
* no unbounded waiting loops

---

### 11.3 System-level progress property

For the system:

```
Single producer + single consumer + SPSCRing
```

guarantee:

> **The system is non-blocking: neither side waits for the other; each call completes in bounded time. If one side stops, the other continues to complete calls (possibly with false).**

In particular:

* producer is never blocked by consumer actions
* consumer is never blocked by producer actions
* stopping one side:
  * does not hang the other side
  * affects only **success rate**, not **termination** of operations

This property is key for:

* hard-RT event publication
* safe degradation when the non-RT consumer stops
* bounded lossy logging architectures.

## 12. Misuse scenarios and undefined behavior

This section lists **forbidden usage scenarios**. Violating them moves the component outside the formal contract and leads to **undefined behavior** in the component model (regardless of whether this becomes an observable failure in practice).

---

### 12.1 Multiple producers

Using more than one producer context for one `SPSCRing` instance is forbidden.

Violation can cause:

* `head_` write races
* element loss
* single-writer invariant violation
* broken publication happens-before chain

Even if writes are "rare", correctness is **not guaranteed**.

---

### 12.2 Multiple consumers

Using more than one consumer context is forbidden.

Violation can cause:

* `tail_` write races
* duplicated reads or element loss
* `pop()` linearizability violations

---

### 12.3 Re-entrant producer (ISR / NMI nesting)

Re-entering `push()` from nested interrupts of the same producer context is forbidden.

Consequences:

* concurrent `head_` modification
* partial element publication
* lost wait-free guarantee

This includes:

* nested IRQ
* NMI over IRQ
* any asynchronous re-entry into `push()`

---

### 12.4 Re-entrant consumer

Same as producer: re-entering `pop()` is forbidden.

Consequences:

* concurrent `tail_` writes
* element loss or duplication
* queue invariant violations

---

### 12.5 Non-trivially-copyable `T`

Using a type `T` that does not satisfy `std::is_trivially_copyable_v<T>` is forbidden.

Possible consequences:

* hidden allocations
* constructor/destructor calls
* unbounded execution time
* hard-RT contract violations
* partially constructed objects under races

Compile-time checks prevent this scenario,
but removing or bypassing them moves the system outside the correctness envelope.

---

### 12.6 Using `empty()` / `full()` for synchronization

Using:

```
empty()
full()
```

for:

* publication decisions
* thread orchestration
* synchronization protocol logic

is forbidden.

Reason:

* relaxed reads
* no happens-before
* possible stale values in **both directions**

Consequence:

> correct logic must rely **only on `push()` / `pop()`**.

---

### 12.7 Ignoring `push()` result

Ignoring:

```
push() == false
```

means **unhandled data loss**.

This is acceptable only if:

* the system is intentionally **lossy**
* losses are explicitly accounted for in architecture
* degradation metrics/signals exist

Otherwise this is a **system-level architectural error**.

---

### 12.8 Operation after consumer shutdown

If consumer is permanently stopped while producer continues:

* `push()` stays wait-free
* queue eventually becomes full
* all subsequent publications are lost

This is **expected bounded-loss behavior**,
but it must be addressed in system shutdown protocols.

---

### 12.9 Violating cache-coherent assumptions

The contract assumes:

* cache-coherent memory
* correct acquire/release atomic behavior

Using this on:

* non-coherent DMA regions
* memory-mapped regions without barriers
* non-standard memory-model platforms

requires **extra validation** outside this contract.

Use with DMA or non-coherent memory requires explicit cache maintenance and memory barriers outside this contract.

---

## 12.10 Summary

All listed scenarios:

* are **not supported by the component**
* are **not required to be detected**
* move the system outside formally proven correctness

Therefore:

> `SPSCRing` correctness is guaranteed
> **only under strict compliance with sections 1-3 of this contract.**
