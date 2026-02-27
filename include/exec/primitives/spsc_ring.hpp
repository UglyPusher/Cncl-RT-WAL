#pragma once

#include "stam/stam.hpp"
#include <atomic>
#include <cstddef>
#include <type_traits>
#include "sys/sys_align.hpp"   // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN

namespace stam::exec::primitives {

/*
 * SPSCRing — Single-Producer Single-Consumer lock-free ring buffer.
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and exactly 1 consumer (reader)
 *  - producer: push-only; consumer: pop-only
 *  - producer is NOT re-entrant (no nested IRQ/NMI calling push())
 *  - consumer is NOT re-entrant
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *  - Capacity must be a power of two and >= 2
 *
 * SEMANTICS:
 *  - Queue / log primitive: every pushed item is delivered in FIFO order.
 *  - No items are lost unless the ring is full (push() returns false).
 *  - Unlike DoubleBuffer/Mailbox2Slot, intermediate items are NOT dropped.
 *
 * RT APPLICABILITY:
 *  - push(): wait-free, O(1), no loops/CAS/mutex/syscalls/allocations
 *  - pop():  wait-free, O(1), no loops/CAS/mutex/syscalls/allocations
 *
 * CAPACITY:
 *  - Usable slots = Capacity - 1 (one slot reserved as full/empty sentinel).
 *
 * MISUSE:
 *  - Violations of the above contract result in undefined behavior
 *    with respect to the intended semantics of this component.
 */

// ============================================================================
// Forward declarations
// ============================================================================

template <typename T, size_t Capacity> class SPSCRingWriter;
template <typename T, size_t Capacity> class SPSCRingReader;

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T, size_t Capacity>
struct SPSCRingCore final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two and >= 2");

    static_assert(std::is_trivially_copyable_v<T>,
                  "SPSCRing requires trivially copyable T");

    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by portability layer");

    // NOTE: Core is an intentional POD-like carrier of shared state.
    // Fields are public to make layout and invariants explicit and auditable.
    // Friend declarations document intent: only Writer/Reader access Core.

    friend class SPSCRingWriter<T, Capacity>;
    friend class SPSCRingReader<T, Capacity>;

    // head_: index of the next slot to write into.
    // Written by writer (release), read by writer (relaxed) + reader (acquire).
    SYS_CACHELINE_ALIGN std::atomic<size_t> head_{0};

    // tail_: index of the next slot to read from.
    // Written by reader (release), read by reader (relaxed) + writer (acquire).
    SYS_CACHELINE_ALIGN std::atomic<size_t> tail_{0};

    // Padding between tail_ and buffer_[0]:
    // Ensures buffer_[0] does not share a cache line with tail_.
    // Without this, a reader advancing tail_ would invalidate the cache line
    // containing the first buffer slots, creating false sharing with the writer.
    char pad_[SYS_CACHELINE_BYTES];

    SYS_CACHELINE_ALIGN T buffer_[Capacity];

    static_assert(std::atomic<size_t>::is_always_lock_free,
                  "std::atomic<size_t> must be lock-free on this platform");
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T, size_t Capacity>
class SPSCRingWriter final {
public:
    explicit SPSCRingWriter(SPSCRingCore<T, Capacity>& core) noexcept
        : core_(core) {}

    SPSCRingWriter(const SPSCRingWriter&)            = delete;
    SPSCRingWriter& operator=(const SPSCRingWriter&) = delete;

    // Move = transfer of producer role (not duplication).
    SPSCRingWriter(SPSCRingWriter&&) noexcept            = default;
    SPSCRingWriter& operator=(SPSCRingWriter&&) noexcept = default;

    // Push an item into the ring (wait-free, bounded).
    // Returns true on success, false if the ring is full.
    //
    // Memory ordering:
    //  - head_ loaded relaxed: producer owns head_, no synchronization needed.
    //  - tail_ loaded acquire: establishes happens-before with reader's
    //    release-store of tail_, ensuring the slot we're about to write
    //    has already been vacated by the reader.
    //  - head_ stored release: makes the written item visible to the reader.
    [[nodiscard]] bool push(const T& item) noexcept {
        const size_t head      = core_.head_.load(std::memory_order_relaxed);
        const size_t next_head = (head + 1) & (Capacity - 1);

        if (next_head == core_.tail_.load(std::memory_order_acquire)) {
            return false;  // ring is full
        }

        core_.buffer_[head] = item;
        core_.head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Approximate occupancy — telemetry only.
    // May return stale values; must not be used for flow control or sync.
    [[nodiscard]] bool full() const noexcept {
        const size_t head      = core_.head_.load(std::memory_order_relaxed);
        const size_t next_head = (head + 1) & (Capacity - 1);
        return next_head == core_.tail_.load(std::memory_order_relaxed);
    }

    static constexpr size_t usable_capacity() noexcept { return Capacity - 1; }

private:
    SPSCRingCore<T, Capacity>& core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T, size_t Capacity>
class SPSCRingReader final {
public:
    explicit SPSCRingReader(SPSCRingCore<T, Capacity>& core) noexcept
        : core_(core) {}

    SPSCRingReader(const SPSCRingReader&)            = delete;
    SPSCRingReader& operator=(const SPSCRingReader&) = delete;

    // Move = transfer of consumer role (not duplication).
    SPSCRingReader(SPSCRingReader&&) noexcept            = default;
    SPSCRingReader& operator=(SPSCRingReader&&) noexcept = default;

    // Pop an item from the ring (wait-free, bounded).
    // Returns true on success, false if the ring is empty.
    //
    // Memory ordering:
    //  - tail_ loaded relaxed: consumer owns tail_, no synchronization needed.
    //  - head_ loaded acquire: establishes happens-before with producer's
    //    release-store of head_, ensuring the item we're about to read
    //    has been fully written by the producer.
    //  - tail_ stored release: makes the vacated slot visible to the producer.
    [[nodiscard]] bool pop(T& item) noexcept {
        const size_t tail = core_.tail_.load(std::memory_order_relaxed);

        if (tail == core_.head_.load(std::memory_order_acquire)) {
            return false;  // ring is empty
        }

        item = core_.buffer_[tail];
        core_.tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    // Approximate occupancy — telemetry only.
    // May return stale values; must not be used for flow control or sync.
    [[nodiscard]] bool empty() const noexcept {
        return core_.tail_.load(std::memory_order_relaxed) ==
               core_.head_.load(std::memory_order_relaxed);
    }

    static constexpr size_t usable_capacity() noexcept { return Capacity - 1; }

private:
    SPSCRingCore<T, Capacity>& core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T, size_t Capacity>
class SPSCRing final {
public:
    SPSCRing() = default;

    SPSCRing(const SPSCRing&)            = delete;
    SPSCRing& operator=(const SPSCRing&) = delete;

    // NOTE: Creating more than one Writer or Reader for the same ring
    // violates the 1P/1C contract and is semantically undefined.
    // Runtime guards are intentionally omitted to keep the RT path minimal.

    [[nodiscard]] SPSCRingWriter<T, Capacity> writer() noexcept {
        return SPSCRingWriter<T, Capacity>(core_);
    }

    [[nodiscard]] SPSCRingReader<T, Capacity> reader() noexcept {
        return SPSCRingReader<T, Capacity>(core_);
    }

    SPSCRingCore<T, Capacity>&       core() noexcept       { return core_; }
    const SPSCRingCore<T, Capacity>& core() const noexcept { return core_; }

private:
    SPSCRingCore<T, Capacity> core_{};
};

} // namespace stam::exec::primitives
