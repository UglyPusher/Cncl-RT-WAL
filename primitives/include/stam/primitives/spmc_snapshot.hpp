#pragma once

#include "stam/stam.hpp"
#include <cassert>
#include <bit>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "stam/sys/sys_align.hpp"      // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN
#include "stam/sys/sys_compiler.hpp"   // SYS_FORCEINLINE, SYS_COMPILER_MSVC
#include "stam/sys/sys_preemption.hpp" // stam::sys::preemption_disable/enable
#include "stam/sys/sys_signal.hpp"
#include "stam/sys/sys_topology.hpp"

namespace stam::primitives {

/*
 * SPMCSnapshot<T, N> — SPMC snapshot channel (latest-wins, wait-free).
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and up to N concurrent consumers (readers)
 *  - writer: write-only; readers: read-only; roles must not be swapped
 *  - writer is NOT re-entrant (no nested IRQ/NMI calling publish())
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *
 * SEMANTICS:
 *  - Snapshot / state channel, NOT a queue or log.
 *  - Intermediate updates may be lost (latest-wins, G5).
 *  - try_read() returns false only before the first publish() (G8).
 *  - After the first publish(), try_read() always returns true.
 *  - No NONE / unavailable state after initialization.
 *
 * SLOT AVAILABILITY THEOREM (K = N + 2):
 *  - At most N readers can hold claims simultaneously (one slot each).
 *  - That leaves at least 2 free slots; at most 1 of them equals published.
 *  - Therefore writer always finds a free, non-published slot. QED.
 *
 * TORN READ EXCLUSION (structural, W-NoOverwritePublished):
 *  - Writer never writes to the published slot (I3).
 *  - Reader reads only the published slot, under claim (I4).
 *  - Together: writer↔reader slot access never overlaps structurally.
 *
 * PLATFORM CONSTRAINT:
 *  - UP-only (single-core + preemptive).
 *    For SMP use SPMCSnapshotSmp.
 *
 *  Validity conditions:
 *    Condition A (uniprocessor): try_read() uses sys_preemption_disable/
 *      enable to protect the load-published → set-busy_mask window (steps
 *      2–3). Physical parallelism is absent; protocol is fully correct.
 *    Condition B (SMP with temporal separation): architecture guarantees
 *      writer finishes publishing before readers begin a new polling tick.
 *
 *  On SMP without Condition B, preemption disable is insufficient:
 *  physical parallelism between cores creates the same narrow race.
 *  See spec §Theoretical Bounds for full analysis and the CAS-model alternative.
 *
 * PREEMPTION SAFETY (uniprocessor):
 *  - try_read() disables preemption around steps 2–3 only (3 atomic ops):
 *    load published → fetch_or busy_mask → fetch_add refcnt.
 *    WCET of the critical section is independent of sizeof(T).
 *  - copy(T) is outside the critical section (symmetric with Mailbox2Slot).
 *  - On SMP: preemption disable is insufficient; see Validity conditions.
 *
 * RT APPLICABILITY:
 *  - publish(): wait-free, O(1). No locks. Bounded WCET.
 *  - try_read(): wait-free, O(1). Preemption disabled only for steps 2–3
 *    (3 atomic ops, independent of sizeof(T)). copy(T) is outside.
 *
 * MISUSE GUARDS:
 *  - writer() may be issued at most once per primitive lifetime.
 *  - reader() may be issued at most N times per primitive lifetime.
 *  - Exceeding either limit triggers fail-fast (assert + abort).
 *
 * SPEC: primitives/docs/SPMCSnapshot — RT Contract & Invariants.md (Rev 6.3)
 */

// ============================================================================
// detail: portable count-trailing-zeros
// ============================================================================

namespace detail {

// Returns the index of the least-significant set bit.
// Precondition: v != 0.
template <class UInt> SYS_FORCEINLINE uint32_t ctz_mask_up(UInt v) noexcept
{
    return static_cast<uint32_t>(std::countr_zero(v));
}

} // namespace detail

// ============================================================================
// Forward declarations
// ============================================================================

template <typename T, uint32_t N> class SPMCSnapshotWriter;
template <typename T, uint32_t N> class SPMCSnapshotReader;
#ifdef STAM_TEST
template <typename T, uint32_t N> class SPMCSnapshotTest;
#endif

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T, uint32_t N> class SPMCSnapshotCore final
{
  public:
    // K = N + 2 slots guarantees writer always finds a free non-published slot.
    static constexpr uint32_t K = N + 2;
    using busy_mask_word_t = stam::sys::signal_mask_t;
    static constexpr uint32_t busy_mask_bits = static_cast<uint32_t>(sizeof(busy_mask_word_t) * 8u);

    // Zero-initialise refcnt; ctrl uses in-class member initializers.
    SPMCSnapshotCore() noexcept
    {
        for (uint32_t i = 0; i < K; ++i)
        {
            refcnt[i].store(0u, std::memory_order_relaxed);
        }
    }

    SPMCSnapshotCore(const SPMCSnapshotCore &) = delete;
    SPMCSnapshotCore &operator=(const SPMCSnapshotCore &) = delete;

    static_assert(N >= 1, "SPMCSnapshot requires at least 1 reader (N >= 1)");
    static_assert(K <= busy_mask_bits, "SPMCSnapshot: K = N+2 must fit in busy_mask word "
                                       "(N <= signal_mask_width - 2)");
    static_assert(N <= 254, "SPMCSnapshot: N must fit in uint8_t refcnt (N <= 254)");
    static_assert(std::is_trivially_copyable_v<T>, "SPMCSnapshot requires trivially copyable T");
    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by the portability layer");

    static_assert(std::atomic<busy_mask_word_t>::is_always_lock_free,
                  "busy_mask atomic word must be lock-free on this platform");
    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be lock-free on this platform");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "std::atomic<bool> must be lock-free on this platform");

    // NOTE: Core is an intentional POD-like carrier of shared state.
    // Fields are public to make layout and invariants explicit and auditable.
    friend class SPMCSnapshotWriter<T, N>;
    friend class SPMCSnapshotReader<T, N>;
#ifdef STAM_TEST
    friend class SPMCSnapshotTest<T, N>;
#endif

  private:
    // ---- Data slots --------------------------------------------------------

    // Each slot occupies an integer number of cachelines.
    // Prevents false sharing between writer filling one slot
    // and concurrent readers copying from another.
    struct alignas(SYS_CACHELINE_BYTES) Slot final
    {
        T value;
    };
    static_assert(sizeof(Slot) % SYS_CACHELINE_BYTES == 0,
                  "Slot must occupy an integer number of cachelines; "
                  "consider padding T or using a wrapper");

    // K slots. Content is undefined before the first publish().
    Slot slots[K];

    // ---- Control block -----------------------------------------------------

    // Isolated on its own cacheline: writer and all readers touch these on
    // every publish() / try_read(), and mixing them with slot data or refcnt
    // would cause unnecessary cache invalidations across all participants.
    //
    //   busy_mask   : readers set/clear bits; writer reads (acquire).
    //                 Bit i == 1  ↔  slot i is currently claimed by ≥1 reader.
    //   published   : writer stores (release); readers load (acquire).
    //                 Always a valid slot index after the first publish().
    //   initialized : writer stores true once (release); readers load (acquire).
    //                 False → no data yet; true → data available forever.
    struct alignas(SYS_CACHELINE_BYTES) Control final
    {
        std::atomic<busy_mask_word_t> busy_mask{0};
        std::atomic<uint8_t> published{0};
        std::atomic<bool> initialized{false};
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
    //   refcnt[i] drops to 0 before busy_mask bit i is cleared.
    //   busy_mask bit i is set before refcnt[i] is incremented.
    //   Therefore: busy_mask bit i == 0  ⇒  refcnt[i] == 0  (strictly).
    SYS_CACHELINE_ALIGN std::atomic<uint8_t> refcnt[K];

    // Publish a new snapshot (wait-free, O(1), bounded WCET).
    //
    // Slot selection (I3, W-NoOverwritePublished):
    //   Step 1: load busy_mask (acquire) — sees all reader-claimed slots.
    //   Step 2: load published (acquire) — current published slot index.
    //   Step 3: candidates = ~busy & ~(1<<pub) & all_mask.
    //           Slots that are neither claimed by a reader nor published.
    //           By the Slot Availability Theorem, candidates != 0 for K=N+2.
    //   Step 4: j = ctz(candidates) — lowest-index free non-published slot.
    //
    // Write + publish:
    //   Step 5: slots[j] = value. Safe: j != pub (I3), busy_mask[j] == 0.
    //   Step 6: published.store(j, release). Establishes happens-before with
    //           readers' load(acquire); all bytes of slots[j] are visible
    //           before published == j becomes observable (G2).
    //   Step 7: initialized.store(true, release). Idempotent signal;
    //           true → true has no visible effect after the first call.
    void publish(const T &value) noexcept
    {
        // Steps 1-2: observe busy and published.
        const busy_mask_word_t busy = ctrl.busy_mask.load(std::memory_order_acquire);
        const uint8_t pub = ctrl.published.load(std::memory_order_acquire);

        // Steps 3-4: select a free non-published slot.
        // all_mask restricts to K valid bits; upper bits of ~busy are irrelevant.
        constexpr busy_mask_word_t all_mask =
            (K == SPMCSnapshotCore<T, N>::busy_mask_bits)
                ? ~busy_mask_word_t{0}
                : ((busy_mask_word_t{1} << K) - busy_mask_word_t{1});
        const busy_mask_word_t candidates = (~busy) & ~(busy_mask_word_t{1} << pub) & all_mask;
        const uint8_t j = static_cast<uint8_t>(detail::ctz_mask_up(candidates));

        // Step 5: write data. j != pub is guaranteed by candidate selection.
        slots[j].value = value;

        // Step 6: atomically switch publication.
        // release: slots[j] bytes are fully written before published == j
        // is observable by any reader's load(acquire).
        ctrl.published.store(j, std::memory_order_release);

        // Step 7: signal initialization (idempotent after the first call).
        ctrl.initialized.store(true, std::memory_order_release);
    }

    // Try to read the latest published snapshot (wait-free, O(1)).
    //
    // Returns false  → no data yet (initialized == false; before first publish).
    // Returns true   → out contains a consistent snapshot of the latest state.
    //
    // After the first publish(), always returns true (G8: no NONE state).
    //
    // Claim protocol (I4, I5):
    //   Step 1: check initialized (acquire). Early-out before first publish.
    //   Step 2: load published slot index i (acquire).
    //   Step 3: SET claim — ORDER CRITICAL, busy_mask before refcnt:
    //             busy_mask |= (1<<i)  acq_rel  ← writer sees slot as busy
    //             refcnt[i] += 1       acq_rel  ← precise counter
    //   Step 4: copy slots[i].value into out.
    //           W-NoOverwritePublished (I3) + I6: writer cannot touch slots[i]
    //           while busy_mask[i] == 1, and published switches only after
    //           write into a different slot j is complete.
    //   Step 5: RELEASE claim — ORDER CRITICAL, refcnt before busy_mask:
    //             refcnt[i] -= 1 (acq_rel). If result was 1 (last reader):
    //             busy_mask &= ~(1<<i) (release).
    //
    // Critical section (preemption disabled): covers steps 2–3 only.
    //   WCET is independent of sizeof(T); copy(T) is outside (step 4).
    //   On SMP, preemption disable is insufficient — see contract above.
    [[nodiscard]] bool try_read(T &out) noexcept
    {
        // Step 1: before first publish no data is available.
        if (!ctrl.initialized.load(std::memory_order_acquire))
        {
            return false;
        }

        // --- critical section: load published + set claim ---
        stam::sys::preemption_disable();

        // Step 2: load published slot index.
        const uint8_t i = ctrl.published.load(std::memory_order_acquire);

        // Step 3: set claim. ORDER CRITICAL: busy_mask before refcnt (I5).
        // busy_mask must be visible to writer before refcnt confirms the claim.
        ctrl.busy_mask.fetch_or(busy_mask_word_t{1} << i, std::memory_order_acq_rel);
        refcnt[i].fetch_add(1u, std::memory_order_acq_rel);

        stam::sys::preemption_enable();
        // --- end critical section ---

        // Step 4: copy data from slot i.
        // Slot i is stable: writer cannot write here while i == published
        // (I3), and published switches only after write to j != i is done.
        out = slots[i].value;

        // Step 5: release claim. ORDER CRITICAL: refcnt before busy_mask (I5).
        // Only the last reader (fetch_sub returns 1) clears the busy_mask bit.
        if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
        {
            ctrl.busy_mask.fetch_and(~(busy_mask_word_t{1} << i), std::memory_order_release);
        }
        return true;
    }
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T, uint32_t N> class SPMCSnapshotWriter final
{
  public:
    static constexpr uint32_t K = SPMCSnapshotCore<T, N>::K;
    using busy_mask_word_t = typename SPMCSnapshotCore<T, N>::busy_mask_word_t;

    explicit SPMCSnapshotWriter(SPMCSnapshotCore<T, N> &core) noexcept : core_(core) {}

    SPMCSnapshotWriter(const SPMCSnapshotWriter &) = delete;
    SPMCSnapshotWriter &operator=(const SPMCSnapshotWriter &) = delete;

    // Move = transfer of producer role (not duplication).
    SPMCSnapshotWriter(SPMCSnapshotWriter &&) noexcept = default;
    SPMCSnapshotWriter &operator=(SPMCSnapshotWriter &&) noexcept = default;

    // Unified snapshot API alias.
    void write(const T &value) noexcept { core_.publish(value); }

  private:
    SPMCSnapshotCore<T, N> &core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T, uint32_t N> class SPMCSnapshotReader final
{
  public:
    static constexpr uint32_t K = SPMCSnapshotCore<T, N>::K;
    using busy_mask_word_t = typename SPMCSnapshotCore<T, N>::busy_mask_word_t;

    explicit SPMCSnapshotReader(SPMCSnapshotCore<T, N> &core) noexcept : core_(core) {}

    SPMCSnapshotReader(const SPMCSnapshotReader &) = delete;
    SPMCSnapshotReader &operator=(const SPMCSnapshotReader &) = delete;

    // Move = transfer of consumer role (not duplication).
    SPMCSnapshotReader(SPMCSnapshotReader &&) noexcept = default;
    SPMCSnapshotReader &operator=(SPMCSnapshotReader &&) noexcept = default;

    // Try to read the latest published snapshot (wait-free, O(1)).
    [[nodiscard]] bool try_read(T &out) noexcept { return (core_.try_read(out)); }

  private:
    SPMCSnapshotCore<T, N> &core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T, uint32_t N> class SPMCSnapshot final
{
    static_assert(!stam::sys::kSystemTopologyIsSmp,
                  "SPMCSnapshot is UP-only. In SMP builds use SPMCSnapshotSmp.");

  public:
    static constexpr uint32_t max_readers = N;

    SPMCSnapshot() = default;

    SPMCSnapshot(const SPMCSnapshot &) = delete;
    SPMCSnapshot &operator=(const SPMCSnapshot &) = delete;

    // NOTE: writer() must be called at most once across the object's lifetime.
    // reader() may be called up to N times; each call yields an independent
    // consumer handle for the same Core.

    [[nodiscard]] SPMCSnapshotWriter<T, N> writer() noexcept
    {
        bool expected = false;
        if (!issued_writer_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
        {
            assert(false && "SPMCSnapshot::writer() already issued");
            std::abort();
        }
        return SPMCSnapshotWriter<T, N>(core_);
    }

    [[nodiscard]] SPMCSnapshotReader<T, N> reader() noexcept
    {
        uint32_t expected = issued_readers_.load(std::memory_order_acquire);
        while (true)
        {
            if (expected >= N)
            {
                assert(false && "SPMCSnapshot::reader() limit exceeded");
                std::abort();
            }
            if (issued_readers_.compare_exchange_weak(
                    expected, expected + 1u, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                break;
            }
        }
        return SPMCSnapshotReader<T, N>(core_);
    }

    SPMCSnapshotCore<T, N> &core() noexcept { return core_; }
    const SPMCSnapshotCore<T, N> &core() const noexcept { return core_; }

  private:
    SPMCSnapshotCore<T, N> core_;
    std::atomic<bool> issued_writer_{false};
    std::atomic<uint32_t> issued_readers_{0};
};

} // namespace stam::primitives
