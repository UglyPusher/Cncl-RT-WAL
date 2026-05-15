# SPMCSnapshotSmp (SPMC Snapshot Channel, SMP-safe)

`primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md` · Revision 1.2 - May 2026

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
seq[0..K-1]      - atomic<uint32_t>, per-slot sequence counters
refcnt[0..K-1]   - atomic<uint8_t>, counted readers holding slot i
busy_mask        - atomic<signal_mask_t>, writer slot-selection coordination mask
published        - atomic<uint8_t>, current published slot index
initialized      - atomic<bool>, false before first publish, true after
```

### Atomic roles

| Atomic | Writer | Readers | Purpose |
|---|---|---|---|
| `seq[i]` | fetch_add(release) before/after slot write | load(acquire) before/after payload copy | torn-read acceptance barrier |
| `busy_mask` | load(acquire) | fetch_or/fetch_and | slot-selection coordination mask |
| `refcnt[i]` | no writes | fetch_add/fetch_sub | counted reader claim/release |
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

## Writer Candidate Selection (K = N + 2)

For a loaded coordination mask, writer selects a slot `j` such that:
* `busy_mask[j] == 0` in the observed mask
* `j != published` (not currently published)

Reason:
* at most `N` readers can hold counted claims simultaneously;
* with `K = N + 2`, at least 2 slots are free;
* at most one free slot is currently published;
* therefore at least one free non-published slot always exists.

Consequence: writer always has a valid candidate and stays wait-free.

Important: `busy_mask`/`refcnt` are a coordination mechanism for slot selection,
not the sole torn-read safety proof. Under concurrent readers sharing one
published slot, there can be transient recycling windows where writer and reader
overlap on a slot selected earlier by that reader. Such overlap is allowed by
the protocol and is rejected by per-slot `seq` verification.

---

## Pseudocode

```cpp
void publish(const T& value) noexcept {
    const uint32_t busy = busy_mask.load(std::memory_order_acquire);
    const uint8_t pub   = published.load(std::memory_order_acquire);

    const uint32_t all_mask   = (1u << K) - 1u;
    const uint32_t candidates = ~busy & ~(1u << pub) & all_mask;
    const uint8_t j           = ctz(candidates);  // candidates != 0 by theorem

    seq[j].fetch_add(1u, std::memory_order_release);  // odd: write in progress
    slots[j] = value;
    seq[j].fetch_add(1u, std::memory_order_release);  // even: stable

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

    const uint32_t s1 = seq[i].load(std::memory_order_acquire);
    if (s1 & 1u) {
        if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            busy_mask.fetch_and(~(1u << i), std::memory_order_release);
        }
        return false;
    }

    T tmp = slots[i];

    const uint32_t s2 = seq[i].load(std::memory_order_acquire);
    if (s2 != s1) {
        if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            busy_mask.fetch_and(~(1u << i), std::memory_order_release);
        }
        return false;
    }

    out = tmp;

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

### I4. Reader copies only under counted claim

Reader copies `slots[i]` only after claim is set and before claim is released.

### I5. Claim/release coordination

Set order:
1. `busy_mask.fetch_or(1<<i, acq_rel)`
2. `refcnt[i].fetch_add(1, acq_rel)`

Release order:
1. `refcnt[i].fetch_sub(1, acq_rel)`
2. if last reader: `busy_mask.fetch_and(~(1<<i), release)`

This coordination helps writer avoid slots observed as reader-busy while keeping
the writer path wait-free. It is not the sole proof that writer-reader slot
overlap is impossible.

### I6. Publication re-verify gate

Reader proceeds to payload-copy verification only if `published` is unchanged
between initial load and post-claim re-check (`i2 == i`). If it changed, the
attempt is a single-shot miss and returns `false`.

### I7. Per-slot sequence acceptance gate

Reader accepts payload only if the same even `seq[i]` value is observed before
and after copying `slots[i]`.

If writer overlaps the copy window:
* the first sequence load may be odd, or
* the second sequence load differs from the first.

In both cases `try_read()` returns `false` and does not publish `tmp` into `out`.

### I8. Initialized monotonicity

`initialized` changes `false -> true` after first publish and never returns to `false`.

---

## Guarantees

### G1. Snapshot consistency

If `try_read(out)` returns `true`, `out` is a stable, non-torn snapshot.

This guarantee is provided by per-slot sequence verification around the payload
copy. If a slot is overwritten during the read window (including ABA-style
republish), `try_read()` returns `false` rather than accepting a torn snapshot.

### G2. SMP memory visibility

`published.store(release)` after slot write plus reader `published.load(acquire)` provides the required happens-before edge for published slot visibility.

### G3. Latest-wins

Reader obtains a recent stable snapshot; intermediate writes may be skipped.
Under concurrent publication, a successful read is not guaranteed to be the
newest value at method return time.

### G4. Bounded per-call cost

`publish` and one `try_read` invocation use a fixed number of atomic ops and one payload copy.

### G5. No delivery guarantee

The primitive is a state channel, not an event queue.

---

## Failure Semantics

`try_read()` returns `false` when:
* no data has been published yet (`initialized == false`),
* publication changed between claim and re-verify (`i2 != i`),
* per-slot sequence verification detects an overlapping slot write.

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
