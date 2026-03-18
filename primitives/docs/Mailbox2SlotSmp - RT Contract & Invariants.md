# Mailbox2SlotSmp (SPSC Snapshot Mailbox, SMP-safe)

`primitives/docs/Mailbox2SlotSmp - RT Contract & Invariants.md` · Revision 1.1 - March 2026

---

## Purpose

A primitive for transferring a **state snapshot** from one writer to one reader
on SMP systems.

**Semantics: latest-wins.** Intermediate publications may be lost.

---

## UP Init Contract

Initialization and wiring are defined as **UP init**:

* all `writer()` / `reader()` issuance and bind steps are executed in a single-thread bootstrap phase;
* scheduler is not running yet;
* parallel/multi-core init for the same primitive instance is not allowed.

Handle issuance guards in code rely on this contract.

---

## Portability Profiles

This contract defines two deployment profiles:

* `strict` profile: requires strict ISO C++ memory-model compliance (payload access must be race-free by C++ definition).
* `platform-optimized` profile: allows the classic per-slot-seq protocol used here, where overlapping payload copies may occur but are rejected by sequence re-verify.

Current implementation targets `platform-optimized` profile and this is the product default.
`strict` profile is documented as a portability target but is not implemented in this class.

---

## Model

### Participants

* **Writer**: exactly one thread/core, write-only.
* **Reader**: exactly one thread/core, read-only.

### Handle issuance contract

* `writer()` may be issued at most once per primitive lifetime.
* `reader()` may be issued at most once per primitive lifetime.
* Exceeding either limit is a hard misuse error: implementation triggers fail-fast (`assert` + `abort`).

### Memory

```
slots[2]          - data slots of type T
seqs[2]           - per-slot sequence counters (even stable, odd write in progress)
ctrl.published    - atomic<uint8_t>, current published slot index {0,1}
ctrl.has_value    - atomic<bool>, false before first publish, true after
```

### Atomic roles

| Atomic | Writer | Reader | Purpose |
|---|---|---|---|
| `seqs[i]` | fetch_add | load | slot write-window verification |
| `published` | store(release), load(relaxed) | load(acquire) | publication index |
| `has_value` | store(release) | load(acquire) | pre-first-publish sentinel |

---

## Protocol

### Writer `publish(value)`

1. `pub = published.load(relaxed)`
2. `j = pub ^ 1` (always write to non-published slot)
3. `seqs[j].fetch_add(1, release)` -> odd (open write window)
4. write `slots[j]`
5. `seqs[j].fetch_add(1, release)` -> even (close write window)
6. `published.store(j, release)`
7. `has_value.store(true, release)` (idempotent after first call)

### Reader `try_read(out)` (single-shot)

1. if `has_value.load(acquire) == false` -> `false`
2. `i = published.load(acquire)`
3. `s1 = seqs[i].load(acquire)`; if odd -> `false`
4. copy `slots[i]` into `out`
5. `s2 = seqs[i].load(acquire)`; if `s1 != s2` -> `false`
6. otherwise `true`

No internal retry is performed by design.

---

## Invariants (Safety)

### I1. Single-writer ownership

Only writer modifies slot payload and publication control atomics.

### I2. Non-published slot write rule

Writer writes only slot `j = published ^ 1`; writer never writes the currently published slot.

### I3. Per-slot seqlock validity

For each slot:
* odd `seq` => write in progress,
* even `seq` => quiescent state.

### I4. Reader acceptance rule

Reader accepts data only if the same even `seq` is observed before and after copy.

### I5. Publication ordering

Writer closes `seq` (even) before `published.store(release)`.  
Reader seeing `published == i` with acquire ordering and stable `seq[i]`
observes a consistent snapshot.

### I6. Initialization monotonicity

`has_value` transitions `false -> true` after first publish and never returns to `false`.

---

## Guarantees

### G1. Snapshot consistency

If `try_read(out)` returns `true`, `out` is a consistent snapshot for this invocation.
This guarantee is protocol-level; strict ISO C++ race-free semantics require `strict` profile implementation.

### G2. Failure semantics

`try_read()` returns `false` when:
* no data published yet (`has_value == false`),
* selected slot is currently being written (`seq` odd),
* concurrent write overlapped copy (`s1 != s2`).

### G3. Progress

* `publish()` is wait-free, O(1).
* `try_read()` is wait-free per invocation, O(1) single-shot.
* Stream-level reader behavior is lock-free (misses possible under contention).

### G4. Latest-wins

Intermediate writes may be lost; channel represents current state, not event history.

---

## Memory Ordering / Happens-Before

Key edges:

1. `slots[j] = value` happens-before `published.store(j, release)` in writer program order.
2. `published.load(acquire)` in reader synchronizes with writer release-store.
3. `seq` pre/post checks ensure reader does not accept an overlapped copy.

The odd-check is a defensive fast-fail guard against transient visibility windows.

---

## Cost Model

Writer:
* 2 atomic RMW (`seq`) + 2 atomic stores (`published`, `has_value`) + payload copy

Reader:
* 2-3 atomic loads (`has_value`, `published`, `seq` pre/post) + payload copy
* no RMW on fast success path

All operations are bounded per call.

---

## Compile-time Requirements

* `std::is_trivially_copyable<T>::value == true`
* `SYS_CACHELINE_BYTES > 0`
* `std::atomic<uint32_t>::is_always_lock_free == true`
* `std::atomic<uint8_t>::is_always_lock_free == true`
* `std::atomic<bool>::is_always_lock_free == true`
* slot size aligned to cacheline multiple (`sizeof(Slot) % SYS_CACHELINE_BYTES == 0`)

---

## Limitations (Non-goals)

* Not an event queue/log.
* Exactly one writer and one reader by contract.
* `try_read()` is single-shot and may miss under contention.
* `has_value` does not distinguish "no data yet" from a valid zero payload after first write.

---

## Relation to Mailbox2Slot

| Property | Mailbox2Slot | Mailbox2SlotSmp |
|---|---|---|
| Platform | UP-only | SMP-safe |
| Reader strategy | claim/verify with lock_state | per-slot seq re-verify |
| `try_read` | single-shot, may return false | single-shot, may return false |
| Preemption masking | required for correctness windows | not required |
| Writer path | wait-free O(1) | wait-free O(1) |

---

## Summary

`Mailbox2SlotSmp<T>` is an SMP-safe SPSC latest-wins snapshot primitive.
It keeps writer path wait-free, uses per-slot sequence validation for reader
consistency, and exposes a deterministic single-shot `try_read()` API suitable
for RT polling loops with sticky-state handling in caller logic.
