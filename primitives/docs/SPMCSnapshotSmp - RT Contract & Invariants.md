# SPMCSnapshotSmp (SPMC Snapshot Channel, SMP-safe)

`primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md` · Revision 1.1 - March 2026

---

## Purpose

A primitive for transferring a **state snapshot** from one writer to multiple readers on SMP.

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

* **Writer**: exactly 1 producer; writes slot data and controls `published` / `initialized`.
* **Readers**: up to `N` concurrent consumers (`N <= signal_mask_width - 2`); claim/release slots via `busy_mask` + `refcnt`.

### Parameters

* `N` - maximum number of readers (compile-time)
* `K = N + 2` - number of data slots

### Memory

```
slots[0..K-1]    - data slots of type T
refcnt[0..K-1]   - atomic<uint8_t>, exact number of readers holding slot i
busy_mask        - atomic<signal_mask_t>, conservative busy bitmap for writer
published        - atomic<uint8_t>, current published slot index
initialized      - atomic<bool>, false before first publish, true after
```

### Atomic roles

| Atomic | Writer | Readers | Purpose |
|---|---|---|---|
| `busy_mask` | load(acquire) | fetch_or/fetch_and | slot claim visibility to writer |
| `refcnt[i]` | no writes | fetch_add/fetch_sub | precise concurrent reader count |
| `published` | store(release), load(acquire) | load(acquire) | published slot index |
| `initialized` | store(release) | load(acquire) | first-publication signal |

### Handle issuance contract

* `writer()` must be called at most once per primitive lifetime.
* `reader()` may be called up to `N` times per primitive lifetime.
* Exceeding either limit is a hard misuse error: implementation triggers fail-fast (`assert` + `abort`).

---

## Progress

* `publish(...)`: **wait-free**, O(1)
* `try_read(...)`: **wait-free per invocation**, O(1), single-shot (no internal retry loop)

Stream-level behavior for reader is lock-free: misses are possible under contention.

---

## Writer Slot Availability Theorem (K = N + 2)

At any time there exists at least one slot `j` such that:
* `busy_mask[j] == 0` (not claimed by readers)
* `j != published` (not currently published)

Reason:
* at most `N` slots can be reader-claimed simultaneously;
* with `K = N + 2`, at least 2 slots are free;
* at most one free slot is currently published;
* therefore at least one free non-published slot always exists.

Consequence: writer always has a valid candidate and stays wait-free.

---

## Pseudocode

```cpp
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint8_t pub   = published.load(std::memory_order_acquire);

    const uint32_t all_mask   = (1u << K) - 1u;
    const uint32_t candidates = ~busy & ~(1u << pub) & all_mask;
    const uint8_t j           = ctz(candidates);  // candidates != 0 by theorem

    slots[j] = value;
    published.store(j, std::memory_order_release);
    initialized.store(true, std::memory_order_release);  // idempotent
}

bool try_read(T& out) noexcept {
    if (!initialized.load(std::memory_order_acquire)) {
        return false;
    }

    const uint8_t i = published.load(std::memory_order_acquire);

    // Claim order is strict: busy_mask before refcnt.
    busy_mask.fetch_or(1u << i, std::memory_order_acq_rel);
    refcnt[i].fetch_add(1u, std::memory_order_acq_rel);

    const uint8_t i2 = published.load(std::memory_order_acquire);
    if (i2 != i) {
        // Release order is strict: refcnt before busy_mask.
        if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            busy_mask.fetch_and(~(1u << i), std::memory_order_release);
        }
        return false;
    }

    out = slots[i];

    if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
        busy_mask.fetch_and(~(1u << i), std::memory_order_release);
    }
    return true;
}
```

---

## Invariants (Safety)

### I1. Single-writer ownership

Only writer modifies slot data and stores `published` / `initialized`.

### I2. Reader claim ownership

Only readers modify `busy_mask` and `refcnt[i]`.

### I3. No write to published slot

Writer chooses `j` from `candidates = ~busy_mask & ~(1 << published) & all_mask`; hence `j != published`.

### I4. Reader reads only claimed slot

Reader copies `slots[i]` only after claim is set and before claim is released.

### I5. Claim/release ordering

Set order:
1. `busy_mask.fetch_or(1<<i, acq_rel)`
2. `refcnt[i].fetch_add(1, acq_rel)`

Release order:
1. `refcnt[i].fetch_sub(1, acq_rel)`
2. if last reader: `busy_mask.fetch_and(~(1<<i), release)`

Strict implication:
`busy_mask[i] == 0 => refcnt[i] == 0`.

### I6. Re-verify gate

Reader accepts data only if `published` is unchanged between initial load and post-claim re-check (`i2 == i`).

### I7. Initialized monotonicity

`initialized` changes `false -> true` after first publish and never returns to `false`.

---

## Guarantees

### G1. Snapshot consistency

If `try_read(out)` returns `true`, `out` is a consistent snapshot for that invocation.

Implementation note (defensive):
reader additionally verifies a per-slot sequence counter around the payload copy.
If a slot is overwritten during the read window (including ABA-style republish),
`try_read()` returns `false` rather than accepting a torn snapshot.

### G2. SMP memory visibility

`published.store(release)` after slot write plus reader `published.load(acquire)` provides the required happens-before edge for published slot visibility.

### G3. Latest-wins

Reader obtains a recent snapshot; intermediate writes may be skipped.

### G4. Bounded per-call cost

`publish` and one `try_read` invocation use a fixed number of atomic ops and one payload copy.

### G5. No delivery guarantee

The primitive is a state channel, not an event queue.

---

## Failure Semantics

`try_read()` returns `false` when:
* no data has been published yet (`initialized == false`),
* publication changed between claim and re-verify (`i2 != i`).

No internal retries are performed.

### Test Coverage Note

The `i2 != i` branch is currently covered by probabilistic diagnostic stress, not by a deterministic forced-interleaving unit test.
Reference: `primitives/tests/spmc_snapshot_smp_test.cpp`, `test_stress_single_shot_miss_rate`.

---

## Type And Compile-time Requirements

* `N >= 1`
* `K = N + 2`
* `K <= signal_mask_width` (`busy_mask` is `signal_mask_t`, so `N <= signal_mask_width - 2`)
* `N <= 254` (`refcnt` is `uint8_t`)
* `std::is_trivially_copyable<T>::value == true`
* `std::atomic<signal_mask_t>::is_always_lock_free == true` (busy_mask word)
* `std::atomic<uint8_t>::is_always_lock_free == true`
* `std::atomic<bool>::is_always_lock_free == true`
* `SYS_CACHELINE_BYTES > 0`
* slot size aligned to cacheline multiple (`sizeof(Slot) % SYS_CACHELINE_BYTES == 0`)

---

## Limitations (Non-goals)

* Not an event queue.
* `N` is fixed at compile time.
* Handle over-issuance (`writer()>1`, `reader()>N`) triggers fail-fast (`assert` + `abort`).

---

## Notes

* This type is the SMP-safe counterpart of UP-only `SPMCSnapshot`.
* Design goal: keep writer wait-free and reader wait-free-per-call without mutexes or retry loops in `try_read()`.
