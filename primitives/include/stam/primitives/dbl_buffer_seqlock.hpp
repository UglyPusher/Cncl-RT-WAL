#pragma once

#include "stam/stam.hpp"
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include "stam/sys/sys_align.hpp"    // SYS_CACHELINE_BYTES
#include "stam/sys/sys_compiler.hpp" // SYS_FORCEINLINE

namespace stam::primitives {
    /*
     * DoubleBufferSeqLock<T> — SPSC snapshot buffer, SMP-safe (SeqLock).
     *
     * CONTRACT (hard requirements):
     *  - exactly 1 producer (writer) and exactly 1 consumer (reader)
     *  - writer: write-only; reader: read-only
     *  - writer is NOT re-entrant (no nested IRQ/NMI calling write())
     *  - reader is NOT re-entrant
     *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
     *
     * PLATFORM CONSTRAINT:
     *  - Default/active profile: platform-optimized.
     *  - SMP-safe in platform-optimized profile. No preemption_disable needed.
     *    UP-only predecessor: DoubleBuffer.
     *  - strict ISO C++ profile requires a race-free payload design
     *    (different implementation).
     *  - NOTE: strict profile is currently NOT implemented in this class.
     *
     * SEMANTICS:
     *  - Snapshot / latest-wins. Intermediate updates may be lost.
     *  - write() always succeeds (wait-free, O(1)).
     *  - read() retries internally until a stable snapshot is obtained
     *    (lock-free, O(1) average, retry-loop under write contention).
     *  - Before the first write(), read() returns value-initialized T.
     *    "No data yet" is semantically indistinguishable from a valid zero snapshot.
     *
     * SEQLOCK TRADE-OFF:
     *  - Reader may transiently observe torn intermediate bytes during a concurrent
     *    write. Such copies are always discarded by the seq re-verify step.
     *    For trivially copyable T this is safe at the protocol level in
     *    platform-optimized profile.
     *  - Wrap-around bound: writer must not outrun one reader attempt by
     *    2^31 full write cycles (2^32 seq increments), otherwise s1 == s2
     *    may match after counter wrap.
     *
     * PROGRESS:
     *  - write(): wait-free, O(1). 2 atomic RMW + payload copy.
     *  - read():  lock-free. 2 acquire loads + payload copy per attempt.
     *
     * MISUSE GUARDS:
     *  - writer() may be issued at most once per primitive lifetime.
     *  - reader() may be issued at most once per primitive lifetime.
     *  - Exceeding either limit triggers fail-fast (assert + abort).
     *
     * SPEC: primitives/docs/DoubleBufferSeqLock - RT Contract & Invariants.md (Rev 1.0)
     */

    template <typename T> class DoubleBufferSeqLockWriter;
    template <typename T> class DoubleBufferSeqLockReader;
#ifdef STAM_TEST
    template <typename T> class DoubleBufferSeqLockTest;
#endif
    // ============================================================================
    // Core (shared state carrier)
    // ============================================================================

    template <typename T>
    class DoubleBufferSeqLockCore final {
        public:
        static_assert(std::is_trivially_copyable_v<T>, "DoubleBufferSeqLock requires trivially copyable T");
        static_assert(SYS_CACHELINE_BYTES > 0, "SYS_CACHELINE_BYTES must be defined by portability layer");
        static_assert(std::atomic<uint32_t>::is_always_lock_free, "std::atomic<uint32_t> must be lock-free on this platform");

        friend class DoubleBufferSeqLockWriter<T>;
        friend class DoubleBufferSeqLockReader<T>;
#ifdef STAM_TEST
        friend class DoubleBufferSeqLockTest<T>;
#endif

        DoubleBufferSeqLockCore() noexcept = default;

        DoubleBufferSeqLockCore(const DoubleBufferSeqLockCore &) = delete;
        DoubleBufferSeqLockCore &operator=(const DoubleBufferSeqLockCore &) = delete;

        private:
        // seq: sequence counter. Even = quiescent. Odd = write in progress.
        // Isolated on its own cacheline: writer and reader both touch it on every op.
        struct alignas(SYS_CACHELINE_BYTES) SeqLine final {
            std::atomic<uint32_t> seq{0};
        };
        SeqLine ctrl;

        // payload: single slot, cacheline-aligned to avoid false sharing with ctrl.
        struct alignas(SYS_CACHELINE_BYTES) Slot final {
            T value{};
        };
        Slot slot;

        // Read the latest published snapshot (lock-free, retry-loop).
        //
        // Guaranteed to eventually return a consistent snapshot: writer is
        // wait-free and advances seq monotonically, so retries are bounded
        // in the lock-free sense.
        //
        // Protocol (per attempt):
        //   Step 1. s1 = seq.load(acquire). If odd: writer active → retry.
        //   Step 2. copy payload.
        //   Step 3. s2 = seq.load(acquire). If s1 != s2: write overlapped → retry.
        //           Same even seq before and after copy → snapshot is stable.
        void read(T &out) noexcept {
            uint32_t s1, s2;
            for (;;) {
                s1 = ctrl.seq.load(std::memory_order_acquire);
                if (s1 & 1u) continue; // writer active

                out = slot.value;

                s2 = ctrl.seq.load(std::memory_order_acquire);
                if (s1 == s2) break; // consistent snapshot
            }
        }

        // Publish a new snapshot (wait-free, O(1)).
        // Protocol:
        //   Step 1. seq → odd  (release): marks write open; reader will retry.
        //   Step 2. write payload (non-atomic; single writer, no WW race).
        //   Step 3. seq → even (release): marks write closed; establishes
        //           happens-before with reader's load(acquire) on matching seq.
        void write(const T &value) noexcept {
            // Step 1: open write window (seq goes odd).
            ctrl.seq.fetch_add(1u, std::memory_order_release);

            // Step 2: write payload. Non-atomic write is safe: only one writer,
            // and the seqlock protocol prevents readers from consuming a torn copy.
            slot.value = value;

            // Step 3: close write window (seq goes even).
            ctrl.seq.fetch_add(1u, std::memory_order_release);
        }

    };

    // ============================================================================
    // Producer view
    // ============================================================================

    template <typename T>
    class DoubleBufferSeqLockWriter final {
    public:
        explicit DoubleBufferSeqLockWriter(DoubleBufferSeqLockCore<T> &core) noexcept
            : core_(core) {}

        DoubleBufferSeqLockWriter(const DoubleBufferSeqLockWriter &) = delete;
        DoubleBufferSeqLockWriter &operator=(const DoubleBufferSeqLockWriter &) = delete;

        // Move = transfer of producer role (not duplication).
        DoubleBufferSeqLockWriter(DoubleBufferSeqLockWriter &&) noexcept = default;
        DoubleBufferSeqLockWriter &operator=(DoubleBufferSeqLockWriter &&) noexcept = default;

        void write(const T &value) noexcept {
            core_.write(value);
        }

    private:
        DoubleBufferSeqLockCore<T> &core_;
    };

    // ============================================================================
    // Consumer view
    // ============================================================================

    template <typename T> class DoubleBufferSeqLockReader final {
    public:
        explicit DoubleBufferSeqLockReader(DoubleBufferSeqLockCore<T> &core) noexcept
            : core_(core) {}

        DoubleBufferSeqLockReader(const DoubleBufferSeqLockReader &) = delete;
        DoubleBufferSeqLockReader &operator=(const DoubleBufferSeqLockReader &) = delete;

        // Move = transfer of consumer role (not duplication).
        DoubleBufferSeqLockReader(DoubleBufferSeqLockReader &&) noexcept = default;
        DoubleBufferSeqLockReader &operator=(DoubleBufferSeqLockReader &&) noexcept = default;

        void read(T &out) noexcept {
            core_.read(out);
        }

        // Unified snapshot API: try_read() always succeeds after internal verify.
        [[nodiscard]] bool try_read(T &out) noexcept {
            core_.read(out);
            return true;
        }

    private:
        DoubleBufferSeqLockCore<T> &core_;
    };

    // ============================================================================
    // Convenience wrapper
    // ============================================================================

    template <typename T> class DoubleBufferSeqLock final {
    public:
        static constexpr uint32_t max_readers = 1u;

        DoubleBufferSeqLock() = default;

        DoubleBufferSeqLock(const DoubleBufferSeqLock &) = delete;
        DoubleBufferSeqLock &operator=(const DoubleBufferSeqLock &) = delete;

        [[nodiscard]] DoubleBufferSeqLockWriter<T> writer() noexcept {
            bool expected = false;
            if (!issued_writer_.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                assert(false && "DoubleBufferSeqLock::writer() already issued");
                std::abort();
            }
            return DoubleBufferSeqLockWriter<T>(core_);
        }

        [[nodiscard]] DoubleBufferSeqLockReader<T> reader() noexcept {
            bool expected = false;
            if (!issued_reader_.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                assert(false && "DoubleBufferSeqLock::reader() already issued");
                std::abort();
            }
            return DoubleBufferSeqLockReader<T>(core_);
        }

        DoubleBufferSeqLockCore<T> &core() noexcept { return core_; }
        const DoubleBufferSeqLockCore<T> &core() const noexcept { return core_; }

    private:
        DoubleBufferSeqLockCore<T> core_;
        std::atomic<bool> issued_writer_{false};
        std::atomic<bool> issued_reader_{false};
    };

} // namespace stam::primitives
