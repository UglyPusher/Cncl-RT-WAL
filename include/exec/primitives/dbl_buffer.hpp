#pragma once

#include "stam/stam.hpp"
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "sys/sys_align.hpp"   // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN

namespace stam::exec::primitives {

/*
 * DoubleBuffer — ping-pong snapshot buffer (last-writer-wins).
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and exactly 1 consumer (reader)
 *  - producer: write-only; consumer: read-only
 *  - producer is NOT re-entrant (no nested IRQ/NMI calling write())
 *  - consumer is NOT re-entrant
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *
 * SAFETY NOTE (preemption / SMP):
 *  - This component does NOT guarantee torn-free snapshots in general
 *    SMP or preemptive systems.
 *  - No-torn-read is guaranteed only if read() and write() do not overlap
 *    in time (including the entire copy of T). Overlap is possible even on
 *    a single CPU: if the consumer is preempted after loading `published`
 *    but before copying buffers[idx], and the producer completes two writes
 *    during that window, the slot the consumer is about to read is recycled.
 *  - The caller is responsible for ensuring non-overlap via scheduling
 *    policy, application-level rate contract, IRQ masking, or a
 *    non-preemptible region. See contract doc §4.1 for sufficient conditions.
 *
 * SEMANTICS:
 *  - Snapshot / frame primitive, NOT a queue/log.
 *  - Intermediate updates may be lost.
 *
 * RT APPLICABILITY:
 *  - write(): wait-free, O(1), no loops/CAS/mutex/syscalls/allocations
 *  - read():  O(1), one acquire load + one copy
 *
 * MISUSE:
 *  - Violations of the above contract result in undefined behavior
 *    with respect to the intended semantics of this component.
 */

// Forward declarations for friend access in DoubleBufferCore.
template <typename T> class DoubleBufferWriter;
template <typename T> class DoubleBufferReader;

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T>
struct DoubleBufferCore final {
    static_assert(std::is_trivially_copyable_v<T>,
        "DoubleBuffer requires trivially copyable T");

    static_assert(SYS_CACHELINE_BYTES > 0,
        "SYS_CACHELINE_BYTES must be defined by portability layer");

    static_assert(std::atomic<uint32_t>::is_always_lock_free,
        "DoubleBuffer requires lock-free std::atomic<uint32_t> for deterministic RT behavior.");

    // NOTE (review item #7 - public fields + friends):
    // Core is intentionally a trivial POD-like carrier of shared state.
    // Fields are public to make layout and invariants explicit and auditable.
    //
    // The following friend declarations serve as *documentation of intent*:
    // only Writer/Reader are supposed to access Core directly, even though
    // access is not technically restricted.

    friend class DoubleBufferWriter<T>;
    friend class DoubleBufferReader<T>;

    struct SYS_CACHELINE_ALIGN Slot final {
        // Fix for review item #1 (false sharing):
        // Each slot occupies its own cache line to avoid producer/consumer
        // ping-pong when sizeof(T) < cache line size.
        T value;
    };

    Slot buffers[2];

    // Index of the currently published slot: 0 or 1.
    // NOTE (review item #6):
    // uint32_t is used instead of bool/uint8_t to preserve lock-free
    // std::atomic guarantees across platforms/toolchains.
    SYS_CACHELINE_ALIGN std::atomic<uint32_t> published{0};
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T>
class DoubleBufferWriter final {
public:
    explicit DoubleBufferWriter(DoubleBufferCore<T>& core) noexcept
        : core_(core) {}

    DoubleBufferWriter(const DoubleBufferWriter&) = delete;
    DoubleBufferWriter& operator=(const DoubleBufferWriter&) = delete;

    // Move = transfer of producer role (not duplication)
    DoubleBufferWriter(DoubleBufferWriter&&) noexcept = default;
    DoubleBufferWriter& operator=(DoubleBufferWriter&&) noexcept = default;

    // Producer-only: publish a new snapshot.
    void write(const T& v) noexcept {
        // NOTE (review item #3 - relaxed load):
        // relaxed is sufficient: producer reads published only to choose
        // the inactive slot. Synchronization is established by the
        // release-store below.
        const uint32_t cur =
            core_.published.load(std::memory_order_relaxed);

        const uint32_t next = cur ^ 1u;

        core_.buffers[next].value = v;

        // Publication point.
        core_.published.store(next, std::memory_order_release);
    }

private:
    DoubleBufferCore<T>& core_;
};

// ============================================================================
// Consumer view
// ============================================================================


template <typename T>
class DoubleBufferReader final {
public:
    explicit DoubleBufferReader(const DoubleBufferCore<T>& core) noexcept
        : core_(core) {}

    DoubleBufferReader(const DoubleBufferReader&) = delete;
    DoubleBufferReader& operator=(const DoubleBufferReader&) = delete;

    DoubleBufferReader(DoubleBufferReader&&) noexcept = default;
    DoubleBufferReader& operator=(DoubleBufferReader&&) noexcept = default;

    // Consumer-only: read the last published snapshot.
    //
    // NOTE (review item #4 - read() before first write()):
    // Because DoubleBufferCore is value-initialized (core_{}),
    // buffers are zero-initialized for trivially copyable T.
    //
    // Therefore, calling read() before the first write() has
    // *defined* behavior at the C++ level (returns zero-initialized data),
    // but *semantically unspecified* meaning:
    //
    //   - the caller cannot distinguish "no data published yet"
    //     from "a valid snapshot with all-zero value".
    //
    // If "no data yet" detection is required, add an explicit
    // initialization flag or version counter on top.
    //
    // NOTE (review item #5):
    // Out-parameter avoids return-value ABI/copies and keeps the primitive
    // predictable and zero-overhead for RT usage.
    void read(T& out) const noexcept {
        const uint32_t idx =
            core_.published.load(std::memory_order_acquire);

        out = core_.buffers[idx].value;
    }

private:
    const DoubleBufferCore<T>& core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T>
class DoubleBuffer final {
public:
    DoubleBuffer() = default;

    DoubleBuffer(const DoubleBuffer&) = delete;
    DoubleBuffer& operator=(const DoubleBuffer&) = delete;

    // NOTE (review item #2 - multiple writers):
    // This is an ARCHITECTURAL constraint, not a runtime-checked one.
    // Creating more than one Writer or Reader for the same buffer
    // violates the 1P/1C contract and is semantically undefined.
    //
    // Runtime guards are intentionally omitted to keep the RT path minimal.
    [[nodiscard]] DoubleBufferWriter<T> writer() noexcept {
        return DoubleBufferWriter<T>(core_);
    }

    [[nodiscard]] DoubleBufferReader<T> reader() const noexcept {
        return DoubleBufferReader<T>(core_);
    }

    // Optional access to core for wiring / placement / inspection.
    DoubleBufferCore<T>& core() noexcept { return core_; }
    const DoubleBufferCore<T>& core() const noexcept { return core_; }

private:
    // Value-initialized on purpose: provides deterministic zero state,
    // while keeping "no data yet" semantically unspecified.
    DoubleBufferCore<T> core_{};
};

} // namespace stam::exec::primitives