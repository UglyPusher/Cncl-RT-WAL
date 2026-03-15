#pragma once

#include "stam/stam.hpp"
#include <cassert>
#include <bit>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "stam/sys/sys_align.hpp"    // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN
#include "stam/sys/sys_compiler.hpp" // SYS_FORCEINLINE, SYS_COMPILER_MSVC
#include "stam/sys/sys_signal.hpp"

namespace stam::primitives
{

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
     * MISUSE GUARDS:
     *  - writer() may be issued at most once per primitive lifetime.
     *  - reader() may be issued at most N times per primitive lifetime.
     *  - Exceeding either limit triggers fail-fast (assert + abort).
     *
     * SPEC: primitives/docs/SPMCSnapshotSmp - RT Contract & Invariants.md (Rev 1.1)
     */

    // ============================================================================
    // detail: portable count-trailing-zeros
    // ============================================================================

    namespace detail
    {

        // Returns the index of the least-significant set bit.
        // Precondition: v != 0.
        template <class UInt>
        SYS_FORCEINLINE uint32_t ctz_mask_smp(UInt v) noexcept
        {
            return static_cast<uint32_t>(std::countr_zero(v));
        }

    } // namespace detail

    // ============================================================================
    // Forward declarations
    // ============================================================================

    template <typename T, uint32_t N>
    class SPMCSnapshotSmpWriter;
    template <typename T, uint32_t N>
    class SPMCSnapshotSmpReader;
#ifdef STAM_TEST
    template <typename T, uint32_t N>
    class SPMCSnapshotSmpTest;
#endif

    // ============================================================================
    // Core (shared state carrier)
    // ============================================================================

    template <typename T, uint32_t N>
    class SPMCSnapshotSmpCore final
    {
    public:
        // K = N + 2 slots guarantees writer always finds a free non-published slot.
        static constexpr uint32_t K = N + 2;
        using busy_mask_word_t = stam::sys::signal_mask_t;
        static constexpr uint32_t busy_mask_bits = static_cast<uint32_t>(sizeof(busy_mask_word_t) * 8u);

        static_assert(N >= 1,
                      "SPMCSnapshotSmp requires at least 1 reader (N >= 1)");
        static_assert(K <= busy_mask_bits,
                      "SPMCSnapshotSmp: K = N+2 must fit in busy_mask word "
                      "(N <= signal_mask_width - 2)");
        static_assert(N <= 254,
                      "SPMCSnapshotSmp: N must fit in uint8_t refcnt (N <= 254)");
        static_assert(std::is_trivially_copyable_v<T>,
                      "SPMCSnapshotSmp requires trivially copyable T");
        static_assert(SYS_CACHELINE_BYTES > 0,
                      "SYS_CACHELINE_BYTES must be defined by the portability layer");

        static_assert(std::atomic<busy_mask_word_t>::is_always_lock_free,
                      "busy_mask atomic word must be lock-free on this platform");
        static_assert(std::atomic<uint8_t>::is_always_lock_free,
                      "std::atomic<uint8_t> must be lock-free on this platform");
        static_assert(std::atomic<bool>::is_always_lock_free,
                      "std::atomic<bool> must be lock-free on this platform");

        friend class SPMCSnapshotSmpWriter<T, N>;
        friend class SPMCSnapshotSmpReader<T, N>;
#ifdef STAM_TEST
        friend class SPMCSnapshotSmpTest<T, N>;
#endif

        // Zero-initialize refcnt; ctrl uses in-class member initializers.
        SPMCSnapshotSmpCore() noexcept
        {
            for (uint32_t i = 0; i < K; ++i)
            {
                refcnt[i].store(0u, std::memory_order_relaxed);
            }
        }

        SPMCSnapshotSmpCore(const SPMCSnapshotSmpCore &) = delete;
        SPMCSnapshotSmpCore &operator=(const SPMCSnapshotSmpCore &) = delete;

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

        Slot slots[K];

        // ---- Per-slot sequence counters ---------------------------------------
        //
        // Defensive torn-read detection (ABA / stale-claim windows):
        // Even though the busy_mask/refcnt protocol is designed to structurally
        // prevent writer↔reader overlap, a per-slot seqlock-style counter gives the
        // reader a definitive way to detect an in-flight overwrite and return false
        // without ever accepting a torn snapshot.
        struct alignas(SYS_CACHELINE_BYTES) Seq final
        {
            static_assert(sizeof(std::atomic<uint32_t>) <= SYS_CACHELINE_BYTES,
                          "Seq: atomic wider than cacheline");

            std::atomic<uint32_t> value{0};

        private:
            std::byte pad_[SYS_CACHELINE_BYTES - sizeof(std::atomic<uint32_t>)];
        };

        static_assert(sizeof(Seq) == SYS_CACHELINE_BYTES,
                      "Seq must be exactly one cacheline");

        Seq seq[K]{};

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
        //   busy_mask bit i set  BEFORE  refcnt[i] incremented (claim).
        //   refcnt[i] drops to 0  BEFORE  busy_mask bit i cleared (release).
        //   Therefore: busy_mask bit i == 0  ⇒  refcnt[i] == 0.
        SYS_CACHELINE_ALIGN std::atomic<uint8_t> refcnt[K];

        // Writer-only flag to avoid repeated initialized.store(true) on hot path.
        bool writer_initialized_ = false;

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
        void publish(const T &value) noexcept
        {
            // Steps 1-2: observe busy and published.
            const busy_mask_word_t busy = ctrl.busy_mask.load(std::memory_order_acquire);
            const uint8_t pub = ctrl.published.load(std::memory_order_acquire);

            // Steps 3-4: select a free non-published slot.
            constexpr busy_mask_word_t all_mask =
                (K == SPMCSnapshotSmpCore<T, N>::busy_mask_bits)
                    ? ~busy_mask_word_t{0}
                    : ((busy_mask_word_t{1} << K) - busy_mask_word_t{1});
            const busy_mask_word_t candidates =
                (~busy) & ~(busy_mask_word_t{1} << pub) & all_mask;
            const uint8_t j = static_cast<uint8_t>(detail::ctz_mask_smp(candidates));

            // Step 5: seqlock begin for slot j (odd => writer in progress).
            (void)seq[j].value.fetch_add(1u, std::memory_order_release);

            // Step 6: write data. j != pub is guaranteed by candidate selection.
            slots[j].value = value;

            // Step 7: seqlock end for slot j (even => stable snapshot).
            (void)seq[j].value.fetch_add(1u, std::memory_order_release);

            // Step 8: atomically switch publication.
            ctrl.published.store(j, std::memory_order_release);

            // Step 9: signal initialization (idempotent after the first call).
            if (!writer_initialized_)
            {
                ctrl.initialized.store(true, std::memory_order_release);
                writer_initialized_ = true;
            }
        }

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
        [[nodiscard]] bool try_read(T &out) noexcept
        {
            // Step 1: before first publish no data is available.
            if (!ctrl.initialized.load(std::memory_order_acquire))
            {
                return false;
            }

            // Step 2: load published slot index.
            const uint8_t i = ctrl.published.load(std::memory_order_acquire);

            // Step 3: set claim. ORDER CRITICAL: busy_mask before refcnt (I5).
            // busy_mask must be visible to writer before refcnt confirms the claim.
            ctrl.busy_mask.fetch_or(busy_mask_word_t{1} << i, std::memory_order_acq_rel);
            refcnt[i].fetch_add(1u, std::memory_order_acq_rel);

            // Step 4: re-verify that published has not changed.
            // If it changed: a new publication occurred after we loaded i;
            // our claim on slot i is stale — release and return false.
            const uint8_t i2 = ctrl.published.load(std::memory_order_acquire);
            if (i2 != i)
            {
                // Release claim (I5): refcnt before busy_mask.
                if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
                {
                    ctrl.busy_mask.fetch_and(~(busy_mask_word_t{1} << i),
                                             std::memory_order_release);
                }
                return false;
            }

            // Step 5: seqlock-style verification around the copy.
            // If the slot is being overwritten (or was overwritten during copy),
            // we must not accept the snapshot. Single-shot: return false.
            const uint32_t s1 = seq[i].value.load(std::memory_order_acquire);
            if ((s1 & 1u) != 0u)
            {
                // Writer in progress on slot i.
                if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
                {
                    ctrl.busy_mask.fetch_and(~(busy_mask_word_t{1} << i),
                                             std::memory_order_release);
                }
                return false;
            }

            T tmp = slots[i].value;
            
            const uint32_t s2 = seq[i].value.load(std::memory_order_acquire);
            if (s2 != s1)
            {
                if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
                {
                    ctrl.busy_mask.fetch_and(~(busy_mask_word_t{1} << i),
                                             std::memory_order_release);
                }
                return false;
            }

            out = tmp;

            // Step 6: release claim. ORDER CRITICAL: refcnt before busy_mask (I5).
            // Only the last reader (fetch_sub returns 1) clears the busy_mask bit.
            if (refcnt[i].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            {
                ctrl.busy_mask.fetch_and(~(busy_mask_word_t{1} << i),
                                         std::memory_order_release);
            }
            return true;
        }
    };

    // ============================================================================
    // Producer view
    // ============================================================================

    template <typename T, uint32_t N>
    class SPMCSnapshotSmpWriter final
    {
    public:
        static constexpr uint32_t K = SPMCSnapshotSmpCore<T, N>::K;
        using busy_mask_word_t = typename SPMCSnapshotSmpCore<T, N>::busy_mask_word_t;

        explicit SPMCSnapshotSmpWriter(SPMCSnapshotSmpCore<T, N> &core) noexcept
            : core_(core) {}

        SPMCSnapshotSmpWriter(const SPMCSnapshotSmpWriter &) = delete;
        SPMCSnapshotSmpWriter &operator=(const SPMCSnapshotSmpWriter &) = delete;

        // Move = transfer of producer role (not duplication).
        SPMCSnapshotSmpWriter(SPMCSnapshotSmpWriter &&) noexcept = default;
        SPMCSnapshotSmpWriter &operator=(SPMCSnapshotSmpWriter &&) noexcept = default;

        // Publish a new snapshot (wait-free, O(1), bounded WCET).

        // Unified snapshot API alias.
        void write(const T &value) noexcept
        {
            core_.publish(value);
        }

    private:
        SPMCSnapshotSmpCore<T, N> &core_;
    };

    // ============================================================================
    // Consumer view
    // ============================================================================

    template <typename T, uint32_t N>
    class SPMCSnapshotSmpReader final
    {
    public:
        static constexpr uint32_t K = SPMCSnapshotSmpCore<T, N>::K;
        using busy_mask_word_t = typename SPMCSnapshotSmpCore<T, N>::busy_mask_word_t;

        explicit SPMCSnapshotSmpReader(SPMCSnapshotSmpCore<T, N> &core) noexcept
            : core_(core) {}

        SPMCSnapshotSmpReader(const SPMCSnapshotSmpReader &) = delete;
        SPMCSnapshotSmpReader &operator=(const SPMCSnapshotSmpReader &) = delete;

        // Move = transfer of consumer role (not duplication).
        SPMCSnapshotSmpReader(SPMCSnapshotSmpReader &&) noexcept = default;
        SPMCSnapshotSmpReader &operator=(SPMCSnapshotSmpReader &&) noexcept = default;

        // Try to read the latest published snapshot (wait-free per invocation, O(1)).
        //
        // Returns false → no data yet (before first publish), or publication
        //                 changed between claim and re-verify (single-shot miss).
        // Returns true  → out contains a consistent snapshot of the latest state.
        [[nodiscard]] bool try_read(T &out) noexcept
        {
            return core_.try_read(out);
        }

    private:
        SPMCSnapshotSmpCore<T, N> &core_;
    };

    // ============================================================================
    // Convenience wrapper
    // ============================================================================

    template <typename T, uint32_t N>
    class SPMCSnapshotSmp final
    {
    public:
        static constexpr uint32_t max_readers = N;

        SPMCSnapshotSmp() = default;

        SPMCSnapshotSmp(const SPMCSnapshotSmp &) = delete;
        SPMCSnapshotSmp &operator=(const SPMCSnapshotSmp &) = delete;

        // NOTE: writer() must be called at most once across the object's lifetime.
        // reader() may be called up to N times; each call yields an independent
        // consumer handle for the same Core.

        [[nodiscard]] SPMCSnapshotSmpWriter<T, N> writer() noexcept
        {
            bool expected = false;
            if (!issued_writer_.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
            {
                assert(false && "SPMCSnapshotSmp::writer() already issued");
                std::abort();
            }
            return SPMCSnapshotSmpWriter<T, N>(core_);
        }

        [[nodiscard]] SPMCSnapshotSmpReader<T, N> reader() noexcept
        {
            uint32_t expected = issued_readers_.load(std::memory_order_acquire);
            while (true)
            {
                if (expected >= N)
                {
                    assert(false && "SPMCSnapshotSmp::reader() limit exceeded");
                    std::abort();
                }
                if (issued_readers_.compare_exchange_weak(expected, expected + 1u,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire))
                {
                    break;
                }
            }
            return SPMCSnapshotSmpReader<T, N>(core_);
        }

        SPMCSnapshotSmpCore<T, N> &core() noexcept { return core_; }
        const SPMCSnapshotSmpCore<T, N> &core() const noexcept { return core_; }

    private:
        SPMCSnapshotSmpCore<T, N> core_;
        std::atomic<bool> issued_writer_{false};
        std::atomic<uint32_t> issued_readers_{0};
    };

} // namespace stam::primitives
