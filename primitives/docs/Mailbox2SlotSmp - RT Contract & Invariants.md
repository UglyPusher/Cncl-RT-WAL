# Mailbox2SlotSmp

Revision: 1.0 (March 2026)

---

## 1. Topology

- Exactly 1 producer (writer)
- Exactly 1 consumer (reader)
- Roles are fixed: producer is write-only, consumer is read-only
- Platform target: SMP-safe

## 2. Progress Guarantees

- `publish(...)`: wait-free, O(1)
- `try_read(...)`: wait-free per invocation (single-shot), O(1)
- Stream-level behavior for reader is lock-free (misses possible under contention)

## 3. API Surface

```cpp
template<class T> class Mailbox2SlotSmpWriter {
public:
    void publish(const T& value) noexcept;
};

template<class T> class Mailbox2SlotSmpReader {
public:
    bool try_read(T& out) noexcept; // true = snapshot copied, false = miss
};
```

Semantics:
- Snapshot/latest-wins
- Intermediate updates may be lost
- `false` means "no stable snapshot this invocation" (sticky-state strategy on caller side)

## 4. Memory Model and Protocol (2 slots + per-slot seq)

State:
- `slots[2]`
- `seq[2]` (per-slot sequence counters)
- `published` in `{0,1}`
- `has_value` flag for pre-first-publish phase

Writer:
1. `j = published ^ 1`
2. `seq[j].fetch_add(1, memory_order_release)`   // odd
3. write `slots[j]`
4. `seq[j].fetch_add(1, memory_order_release)`   // even
5. `published.store(j, memory_order_release)`
6. `has_value.store(true, memory_order_release)`

Reader (single-shot):
1. if `has_value.load(memory_order_acquire) == false` -> return false
2. `i = published.load(memory_order_acquire)`
3. `s1 = seq[i].load(memory_order_acquire)`; if odd -> return false
4. copy `slots[i]`
5. `s2 = seq[i].load(memory_order_acquire)`; if `s1 != s2` -> return false
6. return true

## 5. Invariants

- Per-slot odd `seq[i]` means writer is currently updating `slots[i]`
- Per-slot even `seq[i]` means slot is in a quiescent state
- Reader accepts data only if same even `seq` is observed before and after copy
- Writer publishes slot index only after closing that slot's write (`seq` back to even)
- For a slot loaded via `published.load(acquire)`, steady-state observation is even `seq[published]`.
  The odd-check in reader fast path is a defensive guard against transient races/visibility edge cases.

## 6. Failure Semantics

`try_read()` returns `false` when:
- no publication yet (`has_value == false`)
- selected slot is being written (`seq` odd)
- concurrent write changed the selected slot during copy (`s1 != s2`)

No internal retry loop by design (single-shot RT polling style).

## 7. Cost Model

Writer:
- 2 atomic RMW + 1 atomic store + payload copy

Reader:
- 1-2 atomic loads + payload copy + 1 re-verify load
- no RMW on fast successful path

---

## Notes

- This type is the SMP-safe successor for UP-only `Mailbox2Slot`.
- Chosen baseline is 2-slot + per-slot sequence verification to reduce miss-rate versus 1-slot seqlock while keeping protocol bounded and mutex-free.
