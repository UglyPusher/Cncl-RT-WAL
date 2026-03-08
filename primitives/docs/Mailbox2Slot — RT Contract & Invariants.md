# Mailbox2Slot (SPSC Snapshot Mailbox)

`docs/contracts/Mailbox2Slot.md` · Revision 1.4 - February 2026

---

## Purpose

A primitive for transferring a **state snapshot** from one writer to one reader.

**Semantics: latest-wins.** Intermediate publications may be lost.

---

## UP Init Contract

Initialization and wiring are defined as **UP init**:

* all `writer()` / `reader()` issuance and bind steps are executed in a single-thread bootstrap phase;
* scheduler is not running yet;
* parallel/multi-core init for the same primitive instance is not allowed.

Handle issuance guards in code rely on this contract.

---

## Model

### Participants

* **Writer**: exactly one thread/core; only writer writes slots and changes `pub_state`.
* **Reader**: exactly one thread/core; only reader reads data and changes `lock_state`.

### Handle issuance contract

* `writer()` may be issued at most once per primitive lifetime.
* `reader()` may be issued at most once per primitive lifetime.
* Exceeding either limit is a hard misuse error: implementation triggers fail-fast (`assert` + `abort`).

### Memory

* 2 slots: `S[0]`, `S[1]` of type `T`
* Atomic states:
  * `pub_state in {0,1,2}`: published slot 0/1 or **NONE(2)**
  * `lock_state in {0,1,2}`: reader holds slot 0/1 or **UNLOCKED(2)**

### Atomics and barriers

> **Memory model:** implementation must use `std::atomic<state_t>` (`state_t` is the state type, usually `uint8_t`) from C++11+ with `load(std::memory_order_acquire)` and `store(std::memory_order_release)`. Platform intrinsics are allowed only if they provide equivalent happens-before guarantees on the target architecture. `memory_order_relaxed` is **forbidden** for operations participating in writer-reader happens-before chains.

* `pub_state` and `lock_state` are **lock-free** atomics on target platform
* `pub_state.store(..., release)`, `pub_state.load(..., acquire)`
* `lock_state.store(..., release)`, `lock_state.load(..., acquire)`
* **Exception:** `pub_state.load(relaxed)` in writer I5 branch is allowed because writer is the only **writer** (`single-writer`) for `pub_state`; this is read-my-own-write and does not require extra ordering with reader (ordering is already provided by preceding `lock_state.load(acquire)`).

---

## Initialization

After object construction:

* `pub_state` must be set to `NONE`
* `lock_state` must be set to `UNLOCKED`
* Slot contents `S[0]` and `S[1]` are undefined before first publication

```cpp
// init:
pub_state  = NONE      // 2
lock_state = UNLOCKED  // 2
```

A reader calling `try_read` before first publication always gets `false`.

---

## API semantics

### Writer

`publish(value)` publishes a new state. Internal algorithm:

1. **Critical section** (preemption disabled):
   a. Read `lock_state` once (acquire) and select slot `j != lock_state`.
   b. If `pub_state == j`, perform invalidate: `pub_state = NONE` (release).
   c. Re-enable preemption.
2. **Outside critical section:** write data to `S[j]`, publish `pub_state = j` (release).

Overwrite is allowed: writer may update the same slot repeatedly while reader holds the other one.

#### Critical section boundary rationale

Critical section protects **only** steps (a) and (b): slot selection and marking it unavailable. `copy(T)` is intentionally outside.

**Post-critical-section invariant:** `pub_state != j` or `pub_state == NONE`. Any reader arriving after `sys_preemption_enable()` will not see slot `j` as published and cannot start claim on it. Therefore writing `S[j]` is safe without preemption protection.

**Critical section WCET** does not depend on `sizeof(T)` - only atomic operations are inside.

### Reader

`try_read(out)`:

* performs a bounded attempt to get a consistent snapshot
* if failed (NONE or publication race), returns `false`; reader keeps previous (sticky) state
* **no retry** - on failure reader proceeds to next tick

Claim-verify (steps 1-4) is executed in a critical section (preemption disabled): three atomic operations, independent of `sizeof(T)`. This removes false negatives caused by writer preemption in the `p1` to `p2` window. `copy(T)` is outside the critical section, symmetric with writer.

#### `try_read` postcondition

> On `try_read` completion (regardless of return value `true` or `false`), `lock_state == UNLOCKED` holds. This guarantees lock is never left hanging, including failure on `p2 != p1`.

---

## Pseudocode

```cpp
// init
pub_state  = NONE      // 2
lock_state = UNLOCKED  // 2

// Writer
void publish(const T& value) {
    // --- critical section: slot selection + invalidate ---
    sys_preemption_disable();

    // Step 1: select slot.
    // acquire: happens-before with reader's lock_state.store(release).
    int locked = lock_state.load(std::memory_order_acquire);
    int j = (locked == 1) ? 0 : 1;  // UNLOCKED(2) also maps to 1, valid

    // Step 2: invalidate if slot j is already published (I5).
    // relaxed: writer is single-writer for pub_state; read-my-own-write.
    // no race with reader here: pub_state == j implies lock_state != j.
    if (pub_state.load(std::memory_order_relaxed) == j) {
        pub_state.store(NONE, std::memory_order_release);
    }

    sys_preemption_enable();
    // --- end critical section ---
    // Invariant: pub_state != j  OR  pub_state == NONE.
    // Reader cannot claim j before pub_state.store(j) below.

    // Step 3: data write and publish (outside critical section).
    S[j] = value;
    pub_state.store(j, std::memory_order_release);
}

// Reader
bool try_read(T& out) {
    // --- critical section: claim-verify (3 atomic operations) ---
    sys_preemption_disable();

    int p1 = pub_state.load(std::memory_order_acquire);
    if (p1 == NONE) {
        sys_preemption_enable();
        // lock_state is already UNLOCKED by previous-call postcondition
        return false;
    }
    lock_state.store(p1, std::memory_order_release);
    int p2 = pub_state.load(std::memory_order_acquire);

    sys_preemption_enable();
    // --- end critical section ---
    // Invariant: claim is set (lock_state == p1) or already released (NONE path).
    // copy(T) is outside critical section, symmetric with writer.

    if (p2 != p1) {
        lock_state.store(UNLOCKED, std::memory_order_release);
        return false;
    }
    out = S[p1];  // copy outside critical section
    lock_state.store(UNLOCKED, std::memory_order_release);
    return true;
}
```

---

## Invariants (Safety)

### I1. Single-writer data writes

Only writer writes `S[i]` and modifies `pub_state`.

### I2. Single ownership of lock

Only reader **modifies** `lock_state`. Writer may **read** `lock_state` (for slot selection), but does not modify it.

### I3. No writes into locked slot

Writer reads `lock_state` (acquire) under preemption protection, chooses `j != lock_state`, then exits protection. Re-checking `lock_state` after data write is not required: by that point the post-critical-section invariant guarantees reader cannot start claim on `j` (additional guard is provided by reader verify step, I6).

Formally: writer **does not start** writing slot `i` if `lock_state == i` at decision time.

### I4. Read only locked slot

Reader reads `S[i]` **only if** `lock_state == i` (claim is held for the entire read).

### I5. No writes into published slot without invalidate

If writer plans to write slot `j` and currently `pub_state == j`, writer must (inside critical section):

1. `pub_state = NONE` (release)

After critical section:

2. write data to `S[j]`
3. `pub_state = j` (release)

**Goal:** prevent reader from starting read during write. After step 1, any reader arriving before step 3 sees `pub_state == NONE` and returns `false`.

### I6. Reader claim-verify (stale-pub protection)

Reader must confirm publication did not change during claim:

1. `p1 = pub_state.load(acquire)`
2. if `p1 == NONE` -> `return false` *(lock_state already UNLOCKED by postcondition)*
3. `lock_state.store(p1, release)`
4. `p2 = pub_state.load(acquire)`
5. if `p2 != p1` -> `lock_state = UNLOCKED`, `return false` *(no retry)*

Reader may read `S[p1]` only if `p1 == p2`.

#### ABA note

Can `pub_state` change and return to original value between steps 1 and 4 (`p1 == p2` but data is different)?

No. ABA is structurally excluded. Reasoning:

1. Reader executed `lock_state.store(p1, release)` at step 3.
2. Writer reads `lock_state` with `acquire` inside critical section - by happens-before (release -> acquire), writer **must observe** `lock_state == p1`.
3. By I3 writer does not start writing slot `p1` while `lock_state == p1`.
4. Therefore data in `S[p1]` did not change since original publication.
5. If `p1 == p2`, reader reads a consistent snapshot.

ABA safety is provided by combination of I3, happens-before between reader `store(release)` and writer `load(acquire)`, **and** critical-section atomicity of both participants on preemptible systems.

### Lemma: Safe Slot Availability

> Writer always has an available slot for writing (from I2 and I3). Since there is one reader and it cannot hold more than one lock at a time, at most one slot is locked at any moment. Writer chooses `j != lock_state` (or any slot when `lock_state == UNLOCKED`). Slot `j` is never locked, so writing it is always legal by I3. Writer blocking is impossible; G6 (wait-free) holds unconditionally.

---

## Guarantees

### G1. Snapshot consistency

If `try_read(out)` returns `true`, then `out` is a **consistent copy** of some writer-published state `T`.

### G2. No torn read

Overlap of "writer writes `S[i]`" and "reader reads `S[i]`" is impossible when invariants I3-I6 **and** platform requirements (§Preemption Safety) hold. On SMP this guarantee is not achievable under pure ISO C++ guarantees - see §Known Limitations.

### G3. Latest-at-claim (freshness)

On successful `try_read`, reader gets a state published no later than `lock_state.store(p1, release)` time (step 3 in I6). States published after that may be missing - this is allowed by latest-wins semantics.

### G4. No delivery guarantee

Reader is not guaranteed to observe every publication. Intermediate publications may be overwritten.

### G5. Bounded WCET

* Writer (critical section): fixed number of atomic operations; independent of `sizeof(T)`.
* Writer (full `publish`): critical section + `write(T)` + one atomic publication.
* Reader (critical section): three atomic operations (claim-verify); independent of `sizeof(T)`.
* Reader (full `try_read`): critical section + `copy(T)` + release; no unbounded loops.

### G6. Progress

With correct platform atomic implementation:

* Writer - **wait-free** (does not wait for reader; see Safe Slot Availability lemma)
* Reader - **wait-free** for `try_read` (no retry)

---

## Type `T` requirements

```
std::is_trivially_copyable<T>::value == true
```

Copying `T` is performed under claim (`lock_state`) via plain assignment or `std::memcpy`. Non-trivial copy constructors, virtual functions, and internal synchronization primitives in `T` are not allowed in RT context.

---

## Mandatory compile-time requirements

* `std::atomic<state_t>::is_always_lock_free == true` (`state_t` usually `uint8_t`)
* `sizeof(state_t) == 1` (or another native lock-free width allowed in `sys_config`)
* `std::is_trivially_copyable<T>::value == true`

`pub_state` and `lock_state` are wrapped in `CachelinePadded<A>` - a template that aligns atomic to cache-line boundary and pads to full cache-line size. Layout correctness is confirmed by:

```cpp
static_assert(sizeof(CachelinePadded<std::atomic<uint8_t>>) == SYS_CACHELINE_BYTES);
static_assert(sizeof(Slot) % SYS_CACHELINE_BYTES == 0);
static_assert(sizeof(Mailbox2SlotCore<T>) == 2*sizeof(Slot) + 2*sizeof(CachelinePadded<...>));
```

This guarantees no false sharing between slots and control words - not as an intent, but as a compile-time fact.

---

## Preemption Safety

### Preemptible single-processor systems

On preemptible systems (RTOS, Linux PREEMPT_RT, bare metal with IRQ in writer context) writer must protect **slot selection + invalidate** from preemption.

**Rationale:** without protection, this scenario is possible:

1. Writer reads `lock_state == UNLOCKED`, selects `j`.
2. Writer is preempted by interrupt.
3. Reader runs `try_read`, locks the same slot `j`, reads `S[j]`.
4. Writer resumes with stale `locked`, writes into `S[j]` - **torn read**.

Critical section covers only atomic selection and invalidate operations. `copy(T)` is outside - **interrupt WCET does not depend on `sizeof(T)`**.

**Platform mechanisms** (`sys/sys_preemption.hpp`):

| Platform | Mechanism |
|---|---|
| bare-metal ARM Cortex-M | `__disable_irq()` / `__enable_irq()` |
| FreeRTOS | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` |
| RTEMS | `rtems_interrupt_local_disable/enable()` |
| Linux PREEMPT_RT | `local_irq_save()` / `local_irq_restore()` |

### Reader

`try_read()` protects claim-verify (steps 1-4) with a similar critical section: three atomic operations, independent of `sizeof(T)`. This removes false returns caused by writer preemption between loading `p1` and checking `p2`.

### Non-preemptible systems / cooperative scheduling

On non-preemptible systems (cooperative RTOS, bare-metal superloop without IRQ in writer context), critical sections are technically unnecessary. `sys_preemption_disable/enable` are implemented as no-op.

---

## Limitations (Non-goals and Known Limitations)

### Not an event queue

Primitive does not guarantee delivery of every update.

### No support for multiple readers

Extension requires refcount/epochs/3+ slots.

### Slot pointer usage

In-place reading is allowed only within claim/release; storing slot pointer after release is forbidden.

### Known Limitation: torn read on SMP

On multiprocessor systems (SMP), disabling preemption on one core does not prevent parallel writer/reader access to one slot from another core.

**Scenario:**

1. Writer (core 0) exits critical section: selected `j = kSlot1`, performed invalidate (`pub_state = NONE`).
2. Writer starts writing `S[1]`.
3. Reader (core 1) concurrently sees `pub_state == NONE` -> `false`... but a variant is possible where reader locked `kSlot1` before writer invalidate and passed verify.
4. Writer writes `S[1]`, reader reads `S[1]` - concurrent access to non-atomic `T`.

By C++ memory model this is **undefined behavior**.

**Practical correctness** relies on platform cacheline-coherency guarantees on ARM Cortex and x86-64 for `sizeof(T) <= SYS_CACHELINE_BYTES` and aligned data. This is a **platform guarantee** outside ISO C++.

**Formal verification** under ISO C++ is impossible without atomizing `T`, which is incompatible with this primitive's RT contract. Using `Mailbox2Slot` on SMP is a conscious tradeoff with explicit acceptance of this limitation.

---

## Changelog

| Revision | Date | Changes |
|---|---|---|
| 1.0 | - | Initial version |
| 1.1 | - | ABA proof clarification |
| 1.2 | - | Added section G3, clarified I5 wording |
| 1.3 | Feb 2026 | Memory-order clarification; added Safe Slot Availability lemma |
| 1.4 | Feb 2026 | Critical sections in `publish()` and `try_read()` for preemptible systems; `pub_state.load(relaxed)` in I5 with rationale; `CachelinePadded<>` wrapper + layout `static_assert`; `sole owner` -> `single-writer`; `std::atomic<T>` -> `std::atomic<state_t>` in memory model; §Preemption Safety (writer + reader); §Known Limitations (SMP torn read); clarified G2, G3, G5; init block in pseudocode |
