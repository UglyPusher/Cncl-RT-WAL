# WAL Binary Format — LogRecordV2 (Multi-Producer, 64B)

## 0. Scope

This document specifies the **on-media binary format** of a fixed-size WAL record used for:
- crash/power-loss detectable append-only logging,
- multi-producer event capture via a non-RT coordinator,
- offline analysis and replay.

The format is designed to be:
- **fixed 64 bytes per record**,
- **forward-evolvable** (within 64B envelope),
- **portable across toolchains and architectures**.

This document is the **single source of truth** for decoding/encoding.

---

## 1. Record layout (64 bytes)

`LogRecordV2` occupies exactly 64 bytes.

Byte offsets:

| Off | Size | Field        | Meaning |
|-----|------|--------------|---------|
| 0   | 4    | `crc32`       | CRC over bytes `[4..63]` |
| 4   | 1    | `version`     | Format version (current: `2`) |
| 5   | 1    | `event_type`  | Event type discriminator |
| 6   | 1    | `flags`       | Bit flags / severity |
| 7   | 1    | `producer_id` | Producer identifier |
| 8   | 8    | `global_seq`  | Total order (coordinator assigned) |
| 16  | 8    | `commit_ts`   | Coordinator time (ticks) |
| 24  | 8    | `event_ts`    | Producer time (ticks, shared timebase) |
| 32  | 8    | `producer_seq`| Producer-local monotonic sequence |
| 40  | 10   | `reserved`    | Extension / future use (see §8) |
| 50  | 14   | `payload`     | Event data (type-specific) |

Total = 64 bytes.

---

## 2. Endianness and portability

### 2.1 Endianness
All multi-byte integer fields are encoded as **little-endian** on media:
- `crc32`, `global_seq`, `commit_ts`, `event_ts`, `producer_seq`.

Readers on big-endian hosts MUST byte-swap on decode.

### 2.2 Struct layout is NOT the spec
C/C++ struct packing is an implementation detail. The canonical format is the byte layout in §1.
Writers/readers MUST produce/consume the exact byte layout regardless of compiler padding rules.

---

## 3. CRC / integrity model

### 3.1 CRC field and coverage
`crc32` is computed over bytes **`[4..63]`** (i.e., the record excluding the `crc32` field itself).

### 3.2 CRC algorithm
Unless overridden by build configuration, the default algorithm is:

- **CRC-32C (Castagnoli)**, polynomial `0x1EDC6F41` (reflected form `0x82F63B78`),
- init `0xFFFFFFFF`,
- finalize XOR `0xFFFFFFFF`,
- reflected input/output.

(If you choose “CRC-32 (IEEE)” instead, it MUST be stated in the build/ABI contract and toolchain.)

### 3.3 Validity rule
A record is **valid** iff:
- CRC recomputed over `[4..63]` matches `crc32`, AND
- `version` is supported by the reader (see §4).

Invalid record indicates:
- torn/partial write,
- power-loss tail corruption,
- media corruption.

Readers MUST stop recovery at the first invalid record when scanning forward (see §6).

---

## 4. Versioning / compatibility rules

### 4.1 `version` meaning
`version` describes the **on-media record format**, not the library version.

### 4.2 Compatibility contract
- A reader that supports `version == 2` MUST reject any record with `version != 2`, unless explicitly extended to support other versions.
- Writers MUST write `version = 2` for this format.

### 4.3 Forward evolution policy (within 64B)
Future versions MAY:
- reinterpret `reserved[]` and/or `payload[]` for new event types,
- define new flag bits,
- define extension sub-formats (see §8),
while keeping the 64B envelope and the CRC coverage unchanged.

Any incompatible reinterpretation of fixed offsets in §1 MUST bump `version`.

---

## 5. Semantics of ordering fields

### 5.1 `global_seq` (canonical total order)
Assigned by the single coordinator/writer. Requirements:
- strictly monotonic increasing by 1 for each committed record,
- unique across the entire WAL (across segments/files),
- defines the canonical replay order.

After crash/restart, `global_seq` is recovered by tail-scan:
`next_global_seq = last_valid.global_seq + 1`.

### 5.2 `producer_seq` (per-producer order)
Assigned by each producer locally.
- monotonic increasing within a producer session,
- used for diagnostics: gap detection, loss estimation, local ordering verification.

No global ordering between producers is implied.

---

## 6. Timestamp model (dual timestamps)

### 6.1 Tick units
Both `commit_ts` and `event_ts` are in **ticks** of a fixed unit:

- `TICK = 100 µs` (default; may be adjusted per platform but MUST be stated)

### 6.2 Timebase
- `commit_ts`: taken by coordinator at commit time from a monotonic chip counter or equivalent.
- `event_ts`: taken by producer at event creation time from the same shared monotonic timebase (single chip) when available.

### 6.3 Wrap-around
Timestamps are 32-bit and may wrap. All comparisons MUST be done using modular arithmetic:

To compare times `a` and `b`:
- `delta = (int32_t)(a - b)`
- `a` is after `b` iff `delta > 0`.

Do NOT compare as plain unsigned with `<` across wrap.

### 6.4 Ordering note
Canonical event order is `global_seq`. Timestamp order may differ due to buffering/reordering.

---

## 7. Write protocol (power-loss / torn-write safety)

Writers MUST follow this order per record:

1) Fill bytes `[4..63]` completely (including `version`, timestamps, payload).
2) Compute `crc32` over `[4..63]`.
3) Store/write `crc32` **last**.

Rationale:
- If power is lost mid-record, CRC mismatch prevents false “valid” tail records.

If the underlying media requires explicit flush (filesystems), flushing is non-RT and is outside this format spec.

---

## 8. Reserved / extension policy

`reserved[10]` is reserved for forward evolution.

Rules:
- Unless an extension is formally defined, writers MUST set `reserved[] = 0`.
- Readers MUST ignore `reserved[]` unless they explicitly implement a defined extension.

Recommended extension convention (optional):
- `reserved[0]` = `ext_tag` (0 = none)
- `reserved[1]` = `ext_len` (0..8)
- `reserved[2..9]` = extension data

Any new extension MUST preserve:
- 64B size,
- CRC coverage `[4..63]`,
- write protocol (§7).

---

## 9. Payload interpretation

`payload[26]` is type-specific binary data.

Rules:
- Writers MUST fully define payload semantics per `event_type` in a separate document (or in code).
- Readers must treat payload as opaque unless they understand the corresponding `event_type`.

If variable-length payload is required:
- Encode a length in the first byte(s) of payload, or
- Use `reserved[]` extension scheme (§8).

---

## 10. Segment/file naming (optional, recommended)

If the WAL is segmented into files, recommended naming:

`<boot_id>_<part_id>.seg` with fixed width decimal or hex.

Examples:
- `00000042_00000001.seg`

Sorting by `(boot_id, part_id)` provides a stable iteration order.
`global_seq` remains the canonical order across all segments.

---

## 11. Decoder rules (recovery)

A recovering reader should:

1) Iterate segments in sorted order (if segmented).
2) For each 64B record:
   - verify `version`,
   - verify CRC (§3),
   - stop at first invalid record and ignore the remainder of that segment (tail truncation).
3) Last valid `global_seq` determines `next_global_seq`.

---

## 12. Invariants summary (must hold)

- Record size: **64 bytes**.
- Endianness: **little-endian** on media.
- CRC: computed over bytes **`[4..63]`**, written last.
- `global_seq`: unique and strictly increasing in commit order.
- Timestamps: 32-bit ticks with wrap handled via signed deltas.
- `reserved[]`: zero unless a defined extension is used.

