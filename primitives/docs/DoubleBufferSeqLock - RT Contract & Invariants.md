# DoubleBufferSeqLock - RT Contract & Invariants

## 0. Scope

`DoubleBufferSeqLock<T>` is an SPSC snapshot primitive for transferring state
between two execution contexts:

- **Producer (writer)** - single thread/core, write-only.
- **Consumer (reader)** - single thread/core, read-only.

Designed for:

- latest-state delivery (latest-wins),
- SMP-safe operation without preemption masking,
- deterministic writer path,
- bounded per-attempt reader path with internal retry.

This is **not** a queue and **not** a log.

---

## 0.1 UP Init Contract

Initialization and wiring are defined as **UP init**:

- all `writer()` / `reader()` issuance and bind steps are executed in a single-thread bootstrap phase;
- scheduler is not running yet;
- parallel/multi-core init for the same primitive instance is not allowed.

Handle issuance guards in code rely on this contract.

---

## 1. Semantic Model

`DoubleBufferSeqLock` implements a one-slot seqlock snapshot model:

- Writer opens a write window by making `seq` odd.
- Writer updates payload.
- Writer closes window by making `seq` even.
- Reader accepts payload only if the same even `seq` is seen before and after copy.

Intermediate writer states may be lost; only the latest stable snapshot matters.

---

## 2. Compile-Time Invariants

`T` must satisfy:

```cpp
std::is_trivially_copyable_v<T>
```

Also required:

- `SYS_CACHELINE_BYTES > 0`
- `std::atomic<uint32_t>::is_always_lock_free == true`

Consequences:

- no constructors/destructors on payload path,
- bounded deterministic copy cost,
- lock-free atomic sequence counter.

---

## 3. Runtime Invariants

Notation:

| Symbol | Meaning |
|---|---|
| `seq` | Sequence counter: even = quiescent, odd = write in progress |
| `slot` | Single payload slot |

Invariants:

- Only writer modifies `seq` and payload.
- Reader treats odd `seq` as unstable and retries.
- Reader accepts payload only when `s1 == s2` and both are even.
- `seq` changes by `+1` on open and `+1` on close, preserving monotonic parity protocol.

---

## 4. Threading Model (Preconditions)

- Exactly one writer per instance.
- Exactly one reader per instance.
- Writer is not re-entrant.
- Reader is not re-entrant.

Handle issuance contract (runtime-enforced):

- `writer()` may be issued at most once per primitive lifetime.
- `reader()` may be issued at most once per primitive lifetime.
- Exceeding either limit triggers fail-fast (`assert` + `abort`).

Other violations are undefined behavior relative to this contract.

---

## 5. Memory Ordering / Happens-Before

Writer:

```cpp
seq.fetch_add(1, memory_order_release); // odd
slot = value;
seq.fetch_add(1, memory_order_release); // even
```

Reader loop:

```cpp
s1 = seq.load(memory_order_acquire);
if (s1 is odd) retry;
tmp = slot;
s2 = seq.load(memory_order_acquire);
if (s1 != s2) retry;
out = tmp; // accepted snapshot
```

Guarantee:

- If reader accepts (`s1 == s2`, even), copied payload corresponds to a stable
  interval with no overlapping committed write window.

Note:

- Reader may transiently copy torn bytes during overlap, but such copies are
  discarded by re-verify and never accepted.

---

## 6. Progress Guarantees

Writer (`write()`):

- wait-free, O(1),
- fixed sequence of operations: 2 atomic RMW + 1 payload copy.

Reader (`read()`):

- lock-free (not wait-free),
- per attempt: 2 acquire loads + 1 payload copy,
- retries under contention until stable snapshot is observed.

Reader may spin under heavy continuous writes; system-level scheduling/QoS must
bound this if strict latency is required.

---

## 7. RT Contract

Writer path is suitable for hard-RT:

- bounded WCET,
- no locks, syscalls, or dynamic allocation,
- no dependency on reader state.

Reader path:

- lock-free and bounded per attempt,
- total completion time depends on contention (retry count).

Implication: use in hard-RT reader only if retry budget is acceptable by system
timing analysis.

---

## 8. Initial State

Before first `write()`, payload is value-initialized (`T{}`) and `seq == 0`.

Therefore `read()`/`try_read()` can return a zero value before any publication.
This is C++-defined behavior but semantically cannot distinguish:

- "no data yet"
- "valid zero snapshot"

If this distinction is needed, add an external initialization/version signal.

---

## 9. Error Model

- No error return from `read()` (it retries internally).
- `try_read()` is a unified alias that always returns `true` after internal read.
- No exceptions, no error codes.
- Misuse of handle issuance fails fast by design (`assert` + `abort`).

---

## 10. Misuse Scenarios (Forbidden)

- Multiple writers or multiple readers on the same instance.
- Re-entrant writer/reader calls.
- Non-trivially-copyable `T`.
- Using as event queue/log.
- Assuming every intermediate state is observable.

---

## 11. When to Use DoubleBufferSeqLock

Use when:

- topology is strictly SPSC,
- latest snapshot is needed,
- SMP safety is required,
- writer path must remain wait-free and minimal,
- reader-side retries are acceptable.

Typical examples:

- shared telemetry/state between dedicated writer and consumer thread,
- control/state handoff where occasional retry cost is acceptable.

---

## 12. Relation to DoubleBuffer

| Property | DoubleBuffer | DoubleBufferSeqLock |
|---|---|---|
| Topology | SPSC | SPSC |
| SMP safety | No (UP-only contract) | Yes |
| Writer | wait-free O(1) | wait-free O(1) |
| Reader | O(1), no retry | lock-free retry loop |
| Torn-read handling | requires non-overlap by system contract | overlap tolerated, torn copies discarded |
| "No data yet" signal | none | none |

---

## 13. Summary

`DoubleBufferSeqLock<T>` is an SMP-safe SPSC latest-wins snapshot primitive.
It keeps writer path wait-free and deterministic, and provides reader-side
consistency through sequence verification with lock-free retries.

It is not a queue and not intended for guaranteed delivery of all updates.
