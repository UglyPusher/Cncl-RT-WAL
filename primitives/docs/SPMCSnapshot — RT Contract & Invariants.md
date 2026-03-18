# SPMCSnapshot (SPMC Snapshot Channel)

`primitives/docs/SPMCSnapshot — RT Contract & Invariants.md` · Revision 6.3 - March 2026

---

## Purpose

A primitive for transferring a **state snapshot** from one writer to multiple readers.

**Semantics: latest-wins.** Intermediate publications may be lost.

### Problem class addressed

The primitive solves **safe shared-state publication** under the following conditions:

* **Bounded memory** - `(N+2) × sizeof(T)` for data versus `2N × sizeof(T)` for N independent Mailbox2Slot instances. For large `sizeof(T)` and large N, savings are significant.
* **UP systems (single-core + preemptive)** - this module is UP-only by contract and compile-time guard. For SMP use `SPMCSnapshotSmp`.
* **Preemption** - correctness on UP is preserved if writer or reader is preempted by the scheduler at any point in the algorithm; `try_read()` closes the claim window with a short preemption-disabled section.
* **RT domain** - wait-free, O(1), bounded WCET for writer and all readers. No locks, no dynamic memory, no syscalls.

Torn read is excluded **structurally** (W-NoOverwritePublished invariant) - no verify, no retry, no SeqLock-like counters.

---

## UP Init Contract

Initialization and wiring are defined as **UP init**:

* all `writer()` / `reader()` issuance and bind steps are executed in a single-thread bootstrap phase;
* scheduler is not running yet;
* parallel/multi-core init for the same primitive instance is not allowed.

Handle issuance guards in code rely on this contract.

---

## Comparison with alternatives

| | N × Mailbox2Slot | SPMCSnapshot | SeqLock |
|---|---|---|---|
| Data memory | `2N × sizeof(T)` | `(N+2) × sizeof(T)` | `1 × sizeof(T)` |
| Writer | wait-free, O(N) writes | **wait-free, O(1)** | wait-free, O(1) |
| Reader | wait-free, O(1) | **wait-free, O(1), no verify** | not wait-free |
| Torn read | no | **no (structural)** | no |
| NONE state | no | **no** | no |
| "No data" before first publication | no | **explicit atomic<bool>** | no |
| N fixed | yes | yes | no |

---

## Model

### Participants

* **Writer**: exactly one thread/core; writes slots, controls `published` and `initialized`.
* **Reader[0..N-1]**: N threads/cores; read data and control `refcnt[i]` / `busy_mask`.

### Handle issuance guard (runtime contract)

* `writer()` can be issued exactly once per primitive lifetime.
* `reader()` can be issued at most `N` times per primitive lifetime.
* Exceeding either limit is a hard misuse error: implementation triggers fail-fast (`assert` + `abort`).

### Parameters

* `N` - maximum number of simultaneous readers (fixed at compile time)
* `K = N + 2` - number of slots

### Memory

```
slots[0..K-1]    - data slots of type T, each on a separate cache line
refcnt[0..K-1]   - atomic<uint8_t>, exact count of readers reading slots[i]
busy_mask        - atomic<signal_mask_t>, conservative busy indicator for writer
published        - atomic<uint8_t>, index of current published slot in [0..K-1]
initialized      - atomic<bool>, false before first publication, true after
```

### Atomic roles

| Atomic | Writers | Readers | Purpose |
|---|---|---|---|
| `refcnt[i]` | readers | readers (fetch_sub result) | exact number of concurrent readers |
| `busy_mask` | readers | writer (acquire) | O(1) conservative busy indicator |
| `published` | writer (store) | readers, writer (acquire) | current snapshot index |
| `initialized` | writer (store release, idempotent) | readers (acquire) | first-publication presence signal |

### `initialized` semantics

`initialized` is set to `true` by writer **after** first publication and remains `true` forever. Reader checks it at the start of `try_read` - this is the only `false` return path in steady state.

After set to `true`: `load(acquire)` on x86 is a plain `mov`, with minimal cost. Writer performs `store(true, release)` on each publication - idempotent, `true -> true` creates no races.

### `refcnt` and `busy_mask` coherence invariant

```
busy_mask[i] == 0  ->  refcnt[i] == 0  (strict)
busy_mask[i] == 1  ->  refcnt[i] >= 0  (conservative: bit may lead counter)
```

Reader operation order:
* **Claim set:** `busy_mask.fetch_or(1<<i)` **before** `refcnt[i].fetch_add(1)`
* **Claim release:** `refcnt[i].fetch_sub(1)` **before** `busy_mask.fetch_and(~(1<<i))` (only on 1->0 transition)

---

## Theorem: Writer Slot Availability for K = N+2

> At any time there exists a slot `j` such that:
> * `busy_mask[j] == 0` (not occupied by readers)
> * `j != published`   (not currently published)
>
> meaning writer **always** can pick a free, unblocked,
> unpublished slot for writing.

**Proof:**
Readers can occupy at most `N` slots (each reader holds at most one slot at any moment) -> at least `K - N = 2` slots are free -> at most one of them can be `published` -> at least **one** free non-published slot remains. ∎

**Consequence:** writer never touches the `published` slot. That slot is stable for all readers for the whole interval between `published.store` operations. Verify is unnecessary.

---

## Invariant W-NoOverwritePublished

> Writer never writes to slot `j` where `j == published`.

Direct consequences:
* reader needs no verify - slot cannot be overwritten while published
* `published` is always valid after first publication
* torn read is excluded structurally, not by protocol retries

---

## Object initialization

* `published = 0`
* `initialized = false`
* `busy_mask = 0`
* `refcnt[i] = 0` for all i
* `slots[0..K-1]` contents are undefined before first publication

---

## Pseudocode

```cpp
// Writer
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint8_t  pub  = published.load(std::memory_order_acquire);

    // Pick a free non-published slot.
    // By theorem, with K=N+2 candidates are always non-zero.
    const uint32_t candidates = ~busy & ~(1u << pub);
    const uint8_t  j = static_cast<uint8_t>(__builtin_ctz(candidates));

    // Write data.
    // W-NoOverwritePublished: j != pub guaranteed by slot selection.
    slots[j] = value;

    // Atomically switch publication.
    published.store(j, std::memory_order_release);

    // Initialization signal - idempotent, safe for repeated calls.
    initialized.store(true, std::memory_order_release);
}

// Reader
bool try_read(T& out) noexcept {
    // Step 1: check data availability.
    // After first publication, this load always returns true.
    if (!initialized.load(std::memory_order_acquire)) {
        return false;
    }

    // --- critical section (preemption disabled) ---
    // Closes the window between steps 2 and 3 on UP systems (Condition A).
    // On SMP this is a no-op; race is documented in §Theoretical Bounds.
    sys_preemption_disable();  // platform primitive (sys_preemption.hpp)

    // Step 2: load published slot.
    const uint8_t i = static_cast<uint8_t>(
        published.load(std::memory_order_acquire));

    // Step 3: set claim.
    // ORDER IS CRITICAL: busy_mask before refcnt.
    busy_mask.fetch_or(1u << i, std::memory_order_acq_rel);
    refcnt[i].fetch_add(1, std::memory_order_acq_rel);

    sys_preemption_enable();
    // --- end critical section ---

    // Step 4: read data.
    // No verify required: W-NoOverwritePublished guarantees slot[i] stability.
    // Writer cannot start writing i while i == published,
    // and published changes only after a complete write to j != i.
    out = slots[i];

    // Step 5: release claim.
    // ORDER IS CRITICAL: refcnt before busy_mask.
    if (refcnt[i].fetch_sub(1, std::memory_order_acq_rel) == 1) {
        busy_mask.fetch_and(~(1u << i), std::memory_order_release);
    }
    return true;
}
```

---

## Invariants (Safety)

### I1. Single-writer data ownership

Only writer writes `slots[i]`, and only writer modifies `published` and `initialized`.

### I2. `busy_mask` and `refcnt` ownership

Only readers modify `busy_mask` and `refcnt[i]`. Writer only **reads** `busy_mask` and `published` (acquire).

### I3. W-NoOverwritePublished

Writer chooses slot `j` where `j != published` and `busy_mask[j] == 0`. Writing to `slots[j]` begins only after this choice. `published` switches to `j` only **after** write completion.

### I4. Read only under claim

Reader reads `slots[i]` only while holding a claim: between `fetch_or(1<<i)` and final `fetch_and(~(1<<i))`.

### I5. Claim set/release ordering

**Set** (strict order):
1. `busy_mask.fetch_or(1<<i, acq_rel)` - writer observes occupancy immediately
2. `refcnt[i].fetch_add(1, acq_rel)` - fix reader count

**Release** (strict order):
1. `val = refcnt[i].fetch_sub(1, acq_rel)` - decrement count
2. if `val == 1`: `busy_mask.fetch_and(~(1<<i), release)` - last reader clears bit

### I6. Published-slot stability

From I3: while `published == i`, writer does not write `slots[i]`. `slots[i]` is stable throughout the interval between two consecutive `published.store` calls. Reader needs no verify.

### I7. `initialized` monotonicity

`initialized` transitions from `false` to `true` exactly once - after first publication. Reverse transition is impossible. Once set, all subsequent `load(acquire)` return `true`.

#### ABA note

Can writer publish the same slot `i` twice? Yes. But while a reader holds claim on `i` (`busy_mask[i]==1`, `refcnt[i]>0`), writer cannot select `i` by I3. `slots[i]` data stays stable during the entire read window. ABA does not break correctness.

---

## Guarantees

### G1. Snapshot consistency

`try_read(out)` returns `true` and fills `out` with a consistent copy of the latest published state - always after first publication.

### G2. No torn read (structural)

Overlap of "writer writes `slots[i]`" and "reader reads `slots[i]`" is impossible: writer does not write the published slot (I3), and reader reads only published slot under claim (I4, I6).

### G3. Correctness with N readers on one slot

`refcnt[i]` correctly tracks the number of simultaneous readers. `busy_mask[i]` is cleared only when the last reader finishes.

### G4. Latest-wins

Reader always gets the latest state published at the time of `published.load(acquire)`.

### G5. No delivery guarantee

Intermediate publications may be overwritten.

### G6. Bounded WCET

* Writer: `load(busy_mask)` + `load(published)` + `ctz` + `copy(T)` + `store(published)` + `store(initialized)` - **fixed number of operations**
* Reader: `load(initialized)` + `load(published)` + `fetch_or` + `fetch_add` + `copy(T)` + `fetch_sub` + (conditional) `fetch_and` - **fixed number of operations**

### G7. Progress

* Writer - **wait-free, O(1)**: `ctz(candidates)` deterministically finds a slot. By theorem, `candidates` is always non-zero for K = N+2.
* Reader - **wait-free, O(1)**: fixed sequence without branches affecting the number of atomic operations.

### G8. No NONE state

`published` always contains a valid index in [0..K-1]. There is no data-unavailable window after initialization.

---

## Freshness latency

| Component | Value |
|---|---|
| Writer publish period | `[0, T_w]` |
| Reader poll period | `[0, T_r]` |
| Verify-fail | **none** |
| Hardware visibility | `~0..50 ns` |

**Worst-case: `T_w + T_r`**

This is optimal for a latest-wins primitive with independent writer and reader clocks.

---

## Type `T` requirements

```
std::is_trivially_copyable<T>::value == true
```

---

## Mandatory compile-time requirements

* `N >= 1`, `K = N + 2`
* `K <= signal_mask_width` (that is, `N <= signal_mask_width - 2`)
* `N <= 254` for `refcnt` type `uint8_t`
* `std::atomic<bool>::is_always_lock_free == true`
* `std::atomic<uint8_t>::is_always_lock_free == true`
* `std::atomic<signal_mask_t>::is_always_lock_free == true` (busy_mask word)
* `std::is_trivially_copyable<T>::value == true`
* `kSystemTopologyIsSmp == false` (UP-only build guard)

*(Optional)* place `published`, `initialized`, and `busy_mask` on a separate cache line from `refcnt[]` and data slots.

*(Optional)* round-robin slot choice among `candidates` instead of `ctz` for even slot wear. Does not affect correctness.

---

## Limitations (Non-goals)

* Not an event queue - delivery of every publication is not guaranteed.
* N is fixed at compile time.
* Does not support more than `signal_mask_width - 2` simultaneous readers (`K = N + 2` must fit in `signal_mask_t`).

---

## Theoretical Bounds

This section is informational. Current `SPMCSnapshot` implementation is UP-only by compile-time guard; for SMP use `SPMCSnapshotSmp`.

### Impossibility of strict wait-free on both sides for arbitrary SMP

On an arbitrary SMP system it is **impossible** to simultaneously guarantee:

1. **Wait-free writer** - completes in bounded steps independent of reader actions
2. **Wait-free reader** - completes in bounded steps independent of writer actions
3. **No torn read/write** - consistent data under physically parallel execution

This is not an implementation flaw - it is a fundamental limit.

**Proof sketch (informal):**

At the moment when reader loaded `published == i` but has not yet protected the slot with a claim, there is a window in which writer can physically select slot `i` for writing in parallel. Closing this window is possible only in one of two ways:

* Writer checks whether reader selected the slot - then writer **depends on reader state** -> writer is not wait-free.
* Reader detects conflict and retries - then **reader has retries** -> reader is not wait-free.

Atomic execution of `load(published) + set(busy_mask)` as one operation on two independent memory locations is impossible without a primitive with consensus number >= 2. This is a special case of Herlihy's 1991 consensus hierarchy result.

### Map of achievable guarantees

| Deployment condition | Writer | Reader | Note |
|---|---|---|---|
| UP / single core, no preemption | wait-free | wait-free | No physical parallelism; compiler barrier is sufficient |
| SMP, writer publishes rarely relative to reader tick | wait-free | wait-free in practice, lock-free formally | CAS retry is theoretically possible but practically rare |
| SMP, general case | wait-free | lock-free, bounded retry | CAS model; retries <= number of publications per reader tick |
| SMP, strict wait-free for both | **unachievable** | **unachievable** | Theoretical limit |

### SPMCSnapshot position on this map

SPMCSnapshot v6.3 without verify is correct under:

**Condition A (recommended for RT):** writer and readers run on one core with no preemption inside critical sections (UP or RT core with `SCHED_FIFO` / preemption disabled). No physical parallelism - both sides are strictly wait-free.

`try_read()` calls `sys_preemption_disable/enable` (from `stam/sys/sys_preemption.hpp`) around steps 2-3, closing this window on UP platforms. User must provide platform implementation:

```cpp
// Example: Cortex-M (cmsis_gcc.h)
namespace stam::sys {
    void sys_preemption_disable_impl() noexcept { __disable_irq(); }
    void sys_preemption_enable_impl()  noexcept { __enable_irq();  }
}

// Example: desktop/SMP test (no-op)
namespace stam::sys {
    void sys_preemption_disable_impl() noexcept {}
    void sys_preemption_enable_impl()  noexcept {}
}
```

Critical section WCET does not depend on `sizeof(T)` - it includes only 3 atomic operations. `copy(T)` is done outside the critical section.

**Condition B (SMP with temporal partitioning):** deployment architecture guarantees writer completes publication before readers start a new poll tick. Example: writer on RT core, readers polling on timer with known phase offset.

For **arbitrary SMP without these conditions**, verify after claim set is required (CAS model). Then reader becomes lock-free with bounded retry; writer remains wait-free.

### Note on cross-core data transfer

Cross-core transfer on SMP involves hardware cache-coherency protocol latency (MESI/MOESI): from single digits to hundreds of nanoseconds depending on topology (L1/L2/L3 miss, cross-NUMA). This is an **extra freshness-latency component** not captured by the `T_w + T_r` model - it is platform-specific and must be measured on target hardware.

That is why strict RT architectures minimize inter-core communication: RT tasks are pinned to dedicated cores, data is passed through primitives with explicit barriers, and memory topology is considered during system design.
