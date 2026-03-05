# SPMCSnapshotSmp

Revision: 1.0 (March 2026)

---

## 1. Topology

- Exactly 1 producer (writer)
- Up to `N` concurrent consumers (readers), `N <= 30`
- Roles are fixed: producer is write-only, consumers are read-only
- Platform target: SMP-safe

## 2. Progress Guarantees

- `publish(...)`: wait-free, O(1)
- `try_read(...)`: wait-free per invocation (single-shot), O(1)

## 3. API Surface

```cpp
template<class T, uint32_t N> class SPMCSnapshotSmpWriter {
public:
    void publish(const T& value) noexcept;
};

template<class T, uint32_t N> class SPMCSnapshotSmpReader {
public:
    bool try_read(T& out) noexcept; // true = snapshot copied, false = miss
};
```

Semantics:
- Snapshot/latest-wins
- Intermediate publications may be lost
- `false` means claim invalidated by concurrent publication in this invocation

## 4. Memory Model

Reader (single-shot claim):
1. if `initialized.load(memory_order_acquire) == false` -> return false
2. `i = published.load(memory_order_acquire)`
3. `busy_mask.fetch_or(1u << i, memory_order_acq_rel)`
4. `refcnt[i].fetch_add(1, memory_order_acq_rel)`
5. `i2 = published.load(memory_order_acquire)`
6. if `i2 != i`:
   - `if (refcnt[i].fetch_sub(1, memory_order_acq_rel) == 1)`
     `busy_mask.fetch_and(~(1u << i), memory_order_release)`
   - return false
7. copy `slots[i]`
8. release claim:
   - `if (refcnt[i].fetch_sub(1, memory_order_acq_rel) == 1)`
     `busy_mask.fetch_and(~(1u << i), memory_order_release)`
9. return true

Writer:
1. load `busy_mask` (acquire), load `published` (acquire)
2. pick `j` such that `j != published` and `busy_mask[j] == 0`
3. write `slots[j]`
4. `published.store(j, memory_order_release)`
5. set `initialized` (release) after first successful publish

## 5. Invariants

- Writer never writes currently published slot
- Claimed slot bit in `busy_mask` blocks writer from selecting that slot
- If reader passes re-verify (`published` unchanged), copied slot is stable for this invocation
- At most one writer
- `refcnt[i]` tracks the number of readers currently holding slot `i`
- `busy_mask[i]` is cleared only by the last releaser (`refcnt` transition `1 -> 0`)

## 6. Failure Semantics

`try_read()` returns `false` when:
- no data published yet (`initialized == false`)
- publication changed between claim and re-verify

No retry loop inside `try_read()`: caller decides next polling tick behavior.

## 7. Cost Model

Writer:
- O(1), bounded atomics + one payload copy

Reader:
- O(1), single-shot
- 1 `fetch_or`, 2 `fetch_add/sub` on `refcnt`, optional `fetch_and`,
  2 `published` loads, 1 payload copy

---

## Notes

- This type is the general SMP successor for `SPMCSnapshot` (UP-only / Condition B).
- Design target: preserve wait-free writer and wait-free-per-call readers without mutexes.
