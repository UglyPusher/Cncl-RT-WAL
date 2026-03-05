#pragma once

#include "stam/stam.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "stam/sys/sys_align.hpp"     // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN
#include "stam/sys/sys_compiler.hpp"  // SYS_FORCEINLINE, SYS_COMPILER_MSVC

namespace stam::primitives {

/*
 * SPMCSnapshotSmp<T, N> — SPMC snapshot channel, SMP-safe (latest-wins, wait-free).
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and up to N concurrent consumers (readers)
 *  - writer: write-only; readers: read-only; roles must not be swapped
 *  - writer is NOT re-entrant (no nested IRQ/NMI calling publish())
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *
 * PLATFORM CONSTRAINT:
 *  - SMP-safe. No preemption_disable needed.
 *    UP-only predecessor: SPMCSnapshot (UP-only / Condition B SMP).
 *
 * SEMANTICS:
 *  - Snapshot / state channel, NOT a queue or log.
 *  - Intermediate updates may be lost (latest-wins).
 *  - try_read() returns false only before the first publish().
 *  - After the first publish(), try_read() may return false if publication
 *    changed between claim and re-verify (single-shot: no internal retry).
 *
 * SLOT AVAILABILITY THEOREM (K = N + 2):
 *  - At most N readers can hold claims simultaneously (one slot each).
 *  - That leaves at least 2 free slots; at most 1 of them equals published.
 *  - Therefore writer always finds a free, non-published slot. QED.
 *
 * TORN READ EXCLUSION (structural):
 *  - Writer never writes to the published slot (I3).
 *  - Reader reads only the published slot, under busy_mask claim (I4).
 *  - Together: writer↔reader slot access never overlaps structurally.
 *
 * CLAIM ORDERING INVARIANT (I5):
 *  - busy_mask bit set BEFORE refcnt incremented (claim order).
 *  - refcnt drops to 0 BEFORE busy_mask bit cleared (release order).
 *  - busy_mask[i] == 0 ⇒ refcnt[i] == 0 (strictly).
 *
 * PROGRESS:
 *  - publish(): wait-free, O(1). Bounded atomics + one payload copy.
 *  - try_read(): wait-free per invocation (single-shot), O(1).
 *               1 fetch_or + 1 fetch_add + 2 published loads +
 *               1 payload copy + 1 fetch_sub + optional fetch_and.
 *
 * MISUSE:
 *  - More than 1 simultaneous writer → undefined behavior.
 *  - More than N simultaneous readers → violates Slot Availability Theorem.
 *  - No runtime guard is provided; enforcement is the caller's responsibility.
 *
 * SPEC: primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md (Rev 1.0)
 */

// ============================================================================
// detail: portable count-trailing-zeros
// ============================================================================

namespace detail {

// Returns the index of the least-significant set bit.
// Precondition: v != 0 (undefined behavior if zero, same as __builtin_ctz).
SYS_FORCEINLINE uint32_t ctz32_smp(uint32_t v) noexcept {
#if SYS_COMPILER_MSVC
    unsigned long idx = 0;
    _BitScanForward(&idx, v);
    return static_cast<uint32_t>(idx);
#else
    return static_cast<uint32_t>(__builtin_ctz(v));
#endif
}

} // namespace detail

// ============================================================================
// Forward declarations
// ============================================================================

template <typename T, uint32_t N> class SPMCSnapshotSmpWriter;
template <typename T, uint32_t N> class SPMCSnapshotSmpReader;

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T, uint32_t N>
struct SPMCSnapshotSmpCore final {
    // K = N + 2 slots guarantees writer always finds a free non-published slot.
    static constexpr uint32_t K = N + 2;

    static_assert(N >= 1,
                  "SPMCSnapshotSmp requires at least 1 reader (N >= 1)");
    static_assert(K <= 32,
                  "SPMCSnapshotSmp: K = N+2 must fit in uint32_t busy_mask "
                  "(K <= 32, i.e. N <= 30)");
    static_assert(N <= 254,
                  "SPMCSnapshotSmp: N must fit in uint8_t refcnt (N <= 254)");
    static_assert(std::is_trivially_copyable_v<T>,
                  "SPMCSnapshotSmp requires trivially copyable T");
    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by the portability layer");

    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "std::atomic<uint32_t> must be lock-free on this platform");
    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be lock-free on this platform");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "std::atomic<bool> must be lock-free on this platform");

    friend class SPMCSnapshotSmpWriter<T, N>;
    friend class SPMCSnapshotSmpReader<T, N>;

    // ---- Data slots --------------------------------------------------------

    // Each slot occupies an integer number of cachelines.
    // Prevents false sharing between writer filling one slot
    // and concurrent readers copying from another.
    struct alignas(SYS_CACHELINE_BYTES) Slot final {
        T value;
    };
    static_assert(sizeof(Slot) % SYS_CACHELINE_BYTES == 0,
                  "Slot must occupy an integer number of cachelines; "
                  "consider padding T or using a wrapper");

    Slot slots[K];

    // ---- Control block -----------------------------------------------------

    // Isolated on its own cacheline: writer and all readers touch these on
    // every publish() / try_read().
    //
    //   busy_mask   : readers set/clear bits; writer reads (acquire).
    //                 Bit i == 1  ↔  slot i is currently claimed by ≥1 reader.
    //   published   : writer stores (release); readers load (acquire).
    //                 Always a valid slot index after the first publish().
    //   initialized : writer stores true once (release); readers load (acquire).
    //                 false → no data yet; true → data available forever.
    struct alignas(SYS_CACHELINE_BYTES) Control final {
        std::atomic<uint32_t> busy_mask{0};
        std::atomic<uint8_t>  published{0};
        std::atomic<bool>     initialized{false};
    };
    Control ctrl;

    // ---- Per-slot reference counts -----------------------------------------

    // Written only by readers (fetch_add / fetch_sub).
    // Aligned to a cacheline boundary to isolate from ctrl and slot data.
    //
    //   refcnt[i] == 0  →  no reader is currently copying from slots[i].
    //   refcnt[i] > 0   →  refcnt[i] readers are simultaneously copying.
    //
    // Invariant with busy_mask (I5):
    //   busy_mask bit i set  BEFORE  refcnt[i] incremented (claim).
    //   refcnt[i] drops to 0  BEFORE  busy_mask bit i cleared (release).
    //   Therefore: busy_mask bit i == 0  ⇒  refcnt[i] == 0.
    SYS_CACHELINE_ALIGN std::atomic<uint8_t> refcnt[K];

    // Zero-initialize refcnt; ctrl uses in-class member initializers.
    SPMCSnapshotSmpCore() noexcept {
        for (uint32_t i = 0; i < K; ++i) {
            refcnt[i].store(0u, std::memory_order_relaxed);
        }
    }

    SPMCSnapshotSmpCore(const SPMCSnapshotSmpCore&)            = delete;
    SPMCSnapshotSmpCore& operator=(const SPMCSnapshotSmpCore&) = delete;
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T, uint32_t N>
class SPMCSnapshotSmpWriter final {
public:
    static constexpr uint32_t K = SPMCSnapshotSmpCore<T, N>::K;

    explicit SPMCSnapshotSmpWriter(SPMCSnapshotSmpCore<T, N>& core) noexcept
        : core_(core) {}

    SPMCSnapshotSmpWriter(const SPMCSnapshotSmpWriter&)            = delete;
    SPMCSnapshotSmpWriter& operator=(const SPMCSnapshotSmpWriter&) = delete;

    // Move = transfer of producer role (not duplication).
    SPMCSnapshotSmpWriter(SPMCSnapshotSmpWriter&&) noexcept            = default;
    SPMCSnapshotSmpWriter& operator=(SPMCSnapshotSmpWriter&&) noexcept = default;

    // Publish a new snapshot (wait-free, O(1), bounded WCET).
    //
    // Slot selection (I3, W-NoOverwritePublished):
    //   Step 1: load busy_mask (acquire) — sees all reader-claimed slots.
    //   Step 2: load published (acquire) — current published slot index.
    //   Step 3: candidates = ~busy & ~(1<<pub) & all_mask.
    //           Slots neither claimed by a reader nor published.
    //           By the Slot Availability Theorem, candidates != 0 for K=N+2.
    //   Step 4: j = ctz(candidates) — lowest-index free non-published slot.
    //
    // Write + publish:
    //   Step 5: slots[j] = value. Safe: j != pub (I3), busy_mask[j] == 0.
    //   Step 6: published.store(j, release). Establishes happens-before with
    //           readers' load(acquire); all bytes of slots[j] are visible
    //           before published == j becomes observable (G2).
    //   Step 7: initialized.store(true, release). Idempotent after first call.
    void publish(const T& value) noexcept {
        // Steps 1-2: observe busy and published.
        const uint32_t busy = core_.ctrl.busy_mask.load(std::memory_order_acquire);
        const uint8_t  pub  = core_.ctrl.published.load(std::memory_order_acquire);

        // Steps 3-4: select a free non-published slot.
        const uint32_t all_mask   = (uint32_t(1u) << K) - 1u;
        const uint32_t candidates = ~busy & ~(uint32_t(1u) << pub) & all_mask;
        const uint8_t  j = static_cast<uint8_t>(detail::ctz32_smp(candidates));

        // Step 5: write data. j != pub is guaranteed by candidate selection.
        core_.slots[j].value = value;

        // Step 6: atomically switch publication.
        core_.ctrl.published.store(j, std::memory_order_release);

        // Step 7: signal initialization (idempotent after the first call).
        core_.ctrl.initialized.store(true, std::memory_order_release);
    }

    // Unified snapshot API alias.
    void write(const T& value) noexcept {
        publish(value);
    }

private:
    SPMCSnapshotSmpCore<T, N>& core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T, uint32_t N>
class SPMCSnapshotSmpReader final {
public:
    static constexpr uint32_t K = SPMCSnapshotSmpCore<T, N>::K;

    explicit SPMCSnapshotSmpReader(SPMCSnapshotSmpCore<T, N>& core) noexcept
        : core_(core) {}

    SPMCSnapshotSmpReader(const SPMCSnapshotSmpReader&)            = delete;
    SPMCSnapshotSmpReader& operator=(const SPMCSnapshotSmpReader&) = delete;

    // Move = transfer of consumer role (not duplication).
    SPMCSnapshotSmpReader(SPMCSnapshotSmpReader&&) noexcept            = default;
    SPMCSnapshotSmpReader& operator=(SPMCSnapshotSmpReader&&) noexcept = default;

    // Try to read the latest published snapshot (wait-free per invocation, O(1)).
    //
    // Returns false → no data yet (before first publish), or publication
    //                 changed between claim and re-verify (single-shot miss).
    // Returns true  → out contains a consistent snapshot of the latest state.
    //
    // Claim protocol (I4, I5):
    //   Step 1: check initialized (acquire). Early-out before first publish.
    //   Step 2: load published slot index i (acquire).
    //   Step 3: SET claim — ORDER CRITICAL, busy_mask before refcnt (I5):
    //             busy_mask |= (1<<i)  acq_rel  ← writer sees slot as busy
    //             refcnt[i] += 1       acq_rel  ← precise reader count
    //   Step 4: re-verify published (acquire). If changed: release and return false.
    //   Step 5: copy slots[i].value into out.
    //           W-NoOverwritePublished (I3): writer cannot touch slots[i]
    //           while busy_mask[i] == 1.
    //   Step 6: RELEASE claim — ORDER CRITICAL, refcnt before busy_mask (I5):
    //             refcnt[i] -= 1 (acq_rel). If result was 1 (last reader):
    //             busy_mask &= ~(1<<i) (release).
    [[nodiscard]] bool try_read(T& out) noexcept {
        // Step 1: before first publish no data is available.
        if (!core_.ctrl.initialized.load(std::memory_order_acquire)) {
            return false;
        }

        // Step 2: load published slot index.
        const uint8_t i = core_.ctrl.published.load(std::memory_order_acquire);

        // Step 3: set claim. ORDER CRITICAL: busy_mask before refcnt (I5).
        // busy_mask must be visible to writer before refcnt confirms the claim.
        core_.ctrl.busy_mask.fetch_or(uint32_t(1u) << i, std::memory_order_acq_rel);
        core_.refcnt[i].fetch_add(1u, std::memory_order_acq_rel);

        // Step 4: re-verify that published has not changed.
        // If it changed: a new publication occurred after we loaded i;
        // our claim on slot i is stale — release and return false.
        const uint8_t i2 = core_.ctrl.published.load(std::memory_order_acquire);
        if (i2 != i) {
            // Release claim (I5): refcnt before busy_mask.
            if (core_.refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
                core_.ctrl.busy_mask.fetch_and(~(uint32_t(1u) << i),
                                               std::memory_order_release);
            }
            return false;
        }

        // Step 5: copy data from slot i.
        // Slot i is stable: writer cannot write here while busy_mask[i] == 1 (I3).
        out = core_.slots[i].value;

        // Step 6: release claim. ORDER CRITICAL: refcnt before busy_mask (I5).
        // Only the last reader (fetch_sub returns 1) clears the busy_mask bit.
        if (core_.refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            core_.ctrl.busy_mask.fetch_and(~(uint32_t(1u) << i),
                                           std::memory_order_release);
        }

        return true;
    }

private:
    SPMCSnapshotSmpCore<T, N>& core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T, uint32_t N>
class SPMCSnapshotSmp final {
public:
    SPMCSnapshotSmp() = default;

    SPMCSnapshotSmp(const SPMCSnapshotSmp&)            = delete;
    SPMCSnapshotSmp& operator=(const SPMCSnapshotSmp&) = delete;

    // NOTE: writer() must be called at most once across the object's lifetime.
    // reader() may be called up to N times; each call yields an independent
    // consumer handle for the same Core. Calling reader() more than N times
    // violates the Slot Availability Theorem.
    // No runtime guard is provided; enforcement is the caller's responsibility.

    [[nodiscard]] SPMCSnapshotSmpWriter<T, N> writer() noexcept {
        return SPMCSnapshotSmpWriter<T, N>(core_);
    }

    [[nodiscard]] SPMCSnapshotSmpReader<T, N> reader() noexcept {
        return SPMCSnapshotSmpReader<T, N>(core_);
    }

    SPMCSnapshotSmpCore<T, N>&       core() noexcept       { return core_; }
    const SPMCSnapshotSmpCore<T, N>& core() const noexcept { return core_; }

private:
    SPMCSnapshotSmpCore<T, N> core_;
};

} // namespace stam::primitives
