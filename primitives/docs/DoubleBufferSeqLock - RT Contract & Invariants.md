# DoubleBufferSeqLock

Revision: 1.0 (March 2026)

---

## 1. Topology

- Exactly 1 producer (writer)
- Exactly 1 consumer (reader)
- Roles are fixed: producer is write-only, consumer is read-only
- Platform target: SMP-safe

## 2. Progress Guarantees

- `write(...)`: wait-free, O(1)
- `read(...)`: lock-free, O(1) average, retry-loop under contention

## 3. API Surface

```cpp
template<class T> class DoubleBufferSeqLockWriter {
public:
    void write(const T& value) noexcept;
};

template<class T> class DoubleBufferSeqLockReader {
public:
    void read(T& out) noexcept; // may internally retry until stable snapshot
};
```

Semantics:
- Snapshot/latest-wins
- Intermediate writer states may be lost
- `read()` returns a consistent snapshot (after successful internal verify)

## 4. Memory Model

Writer:
1. `seq.fetch_add(1, memory_order_release)`  // odd = write open
2. write payload bytes
3. `seq.fetch_add(1, memory_order_release)`  // even = write closed

Reader loop:
1. `s1 = seq.load(memory_order_acquire)`
2. if `s1` odd -> retry
3. copy payload
4. `s2 = seq.load(memory_order_acquire)`
5. if `s1 != s2` -> retry
6. success

## 5. Invariants

- Even `seq`: no writer in critical write section
- Odd `seq`: writer is updating payload
- Reader accepts data only when the same even `seq` is observed before and after copy
- Producer is single-writer for `seq` and payload bytes
- Known seqlock trade-off: if writer overlaps reader copy, torn intermediate bytes
  may be observed transiently, but the reader discards such copy via seq re-verify.

## 6. Failure/Retry Semantics

- No explicit `false` return
- Reader retries internally until stable snapshot is obtained
- Under heavy write pressure reader may spin; lock-free guarantee still holds
- Before first `write()`, `read()` returns value-initialized storage content.
  "No data yet" is semantically indistinguishable from a valid zero snapshot.

## 7. Cost Model

Writer fast path:
- 2 atomic RMW + 1 payload copy

Reader per attempt:
- 2 atomic loads + 1 payload copy

---

## Notes

- This type is the SMP-safe successor for UP-only `DoubleBuffer`.
- Intended as a low-overhead SPSC snapshot primitive with structural torn-read exclusion via sequence verification.
