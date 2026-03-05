#pragma once

#include "stam/stam.hpp"
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "stam/sys/sys_align.hpp"     // SYS_CACHELINE_BYTES
#include "stam/sys/sys_compiler.hpp"  // SYS_FORCEINLINE

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
 *  - SMP-safe. No preemption_disable needed.
 *    UP-only predecessor: DoubleBuffer.
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
 *    For trivially copyable T this is safe at the protocol level.
 *
 * PROGRESS:
 *  - write(): wait-free, O(1). 2 atomic RMW + payload copy.
 *  - read():  lock-free. 2 acquire loads + payload copy per attempt.
 *
 * SPEC: primitives/docs/DoubleBufferSeqLock - RT Contract & Invariants.md (Rev 1.0)
 */

template <typename T> class DoubleBufferSeqLockWriter;
template <typename T> class DoubleBufferSeqLockReader;

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T>
struct DoubleBufferSeqLockCore final {
    static_assert(std::is_trivially_copyable_v<T>,
                  "DoubleBufferSeqLock requires trivially copyable T");
    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by portability layer");
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "std::atomic<uint32_t> must be lock-free on this platform");

    friend class DoubleBufferSeqLockWriter<T>;
    friend class DoubleBufferSeqLockReader<T>;

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

    DoubleBufferSeqLockCore() noexcept = default;

    DoubleBufferSeqLockCore(const DoubleBufferSeqLockCore&)            = delete;
    DoubleBufferSeqLockCore& operator=(const DoubleBufferSeqLockCore&) = delete;
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T>
class DoubleBufferSeqLockWriter final {
public:
    explicit DoubleBufferSeqLockWriter(DoubleBufferSeqLockCore<T>& core) noexcept
        : core_(core) {}

    DoubleBufferSeqLockWriter(const DoubleBufferSeqLockWriter&)            = delete;
    DoubleBufferSeqLockWriter& operator=(const DoubleBufferSeqLockWriter&) = delete;

    // Move = transfer of producer role (not duplication).
    DoubleBufferSeqLockWriter(DoubleBufferSeqLockWriter&&) noexcept            = default;
    DoubleBufferSeqLockWriter& operator=(DoubleBufferSeqLockWriter&&) noexcept = default;

    // Publish a new snapshot (wait-free, O(1)).
    //
    // Protocol:
    //   Step 1. seq → odd  (release): marks write open; reader will retry.
    //   Step 2. write payload (non-atomic; single writer, no WW race).
    //   Step 3. seq → even (release): marks write closed; establishes
    //           happens-before with reader's load(acquire) on matching seq.
    void write(const T& value) noexcept {
        // Step 1: open write window (seq goes odd).
        core_.ctrl.seq.fetch_add(1u, std::memory_order_release);

        // Step 2: write payload. Non-atomic write is safe: only one writer,
        // and the seqlock protocol prevents readers from consuming a torn copy.
        core_.slot.value = value;

        // Step 3: close write window (seq goes even).
        core_.ctrl.seq.fetch_add(1u, std::memory_order_release);
    }

private:
    DoubleBufferSeqLockCore<T>& core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T>
class DoubleBufferSeqLockReader final {
public:
    explicit DoubleBufferSeqLockReader(DoubleBufferSeqLockCore<T>& core) noexcept
        : core_(core) {}

    DoubleBufferSeqLockReader(const DoubleBufferSeqLockReader&)            = delete;
    DoubleBufferSeqLockReader& operator=(const DoubleBufferSeqLockReader&) = delete;

    // Move = transfer of consumer role (not duplication).
    DoubleBufferSeqLockReader(DoubleBufferSeqLockReader&&) noexcept            = default;
    DoubleBufferSeqLockReader& operator=(DoubleBufferSeqLockReader&&) noexcept = default;

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
    void read(T& out) noexcept {
        uint32_t s1, s2;
        do {
            s1 = core_.ctrl.seq.load(std::memory_order_acquire);
            if (s1 & 1u) { continue; }  // writer is active, spin

            out = core_.slot.value;

            s2 = core_.ctrl.seq.load(std::memory_order_acquire);
        } while (s1 != s2);
    }

    // Unified snapshot API: try_read() always succeeds after internal verify.
    [[nodiscard]] bool try_read(T& out) noexcept {
        read(out);
        return true;
    }

private:
    DoubleBufferSeqLockCore<T>& core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T>
class DoubleBufferSeqLock final {
public:
    DoubleBufferSeqLock() = default;

    DoubleBufferSeqLock(const DoubleBufferSeqLock&)            = delete;
    DoubleBufferSeqLock& operator=(const DoubleBufferSeqLock&) = delete;

    [[nodiscard]] DoubleBufferSeqLockWriter<T> writer() noexcept {
        return DoubleBufferSeqLockWriter<T>(core_);
    }

    [[nodiscard]] DoubleBufferSeqLockReader<T> reader() noexcept {
        return DoubleBufferSeqLockReader<T>(core_);
    }

    DoubleBufferSeqLockCore<T>&       core() noexcept       { return core_; }
    const DoubleBufferSeqLockCore<T>& core() const noexcept { return core_; }

private:
    DoubleBufferSeqLockCore<T> core_;
};

} // namespace stam::primitives
