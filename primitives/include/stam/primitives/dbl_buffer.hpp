/* DoubleBuffer — ping-pong snapshot buffer (last-writer-wins).
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and exactly 1 consumer (reader)
 *  - producer: write-only; consumer: read-only
 *  - producer is NOT re-entrant (no nested IRQ/NMI calling write())
 *  - consumer is NOT re-entrant
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *
 * PLATFORM CONSTRAINT:
 *  - UP-only (single-core + preemptive). Not safe on SMP.
 *    For SMP use DoubleBufferSeqLock (pending).
 *
 * SAFETY NOTE (preemption / SMP):
 *  - This component does NOT guarantee torn-free snapshots in general
 *    SMP or preemptive systems.
 *  - No-torn-read is guaranteed only if read() and write() do not overlap
 *    in time (including the entire copy of T). Overlap is possible even on
 *    a single CPU: if the consumer is preempted after loading `published`
 *    but before copying buffers[idx], and the producer completes two writes
 *    during that window, the slot the consumer is about to read is recycled.
 *  - Such overlap is outside this contract and must be treated as undefined
 *    behavior for snapshot integrity. Torn reads are a typical symptom, not
 *    a guaranteed or bounded failure mode.
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
 *  - writer() may be issued at most once per primitive lifetime.
 *  - reader() may be issued at most once per primitive lifetime.
 *  - Exceeding either limit triggers fail-fast (assert + abort).
 *  - Other contract violations result in undefined behavior with respect
 *    to the intended semantics of this component.
 *
 * Core is intentionally encapsulated; production code should interact
 * with the primitive only via role views (Writer/Reader).
 * Tests may audit layout/fields via a dedicated friend under STAM_TEST.
 */

#pragma once

#include <cassert>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include "stam/sys/sys_align.hpp" // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN
#include "stam/sys/sys_topology.hpp"

namespace stam::primitives {

// Forward declarations for friend access in DoubleBufferCore.
template <typename T> class DoubleBufferWriter;
template <typename T> class DoubleBufferReader;
#ifdef STAM_TEST
template <typename T> class DoubleBufferCoreTest;
#endif

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T> class DoubleBufferCore final
{
    static_assert(std::is_trivially_copyable_v<T>, "DoubleBuffer requires trivially copyable T");
    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by portability layer");
    static_assert(
        std::atomic<uint32_t>::is_always_lock_free,
        "DoubleBuffer requires lock-free std::atomic<uint32_t> for deterministic RT behavior.");

    friend class DoubleBufferWriter<T>;
    friend class DoubleBufferReader<T>;
#ifdef STAM_TEST
    friend class DoubleBufferCoreTest<T>;
#endif

  private:
    struct SYS_CACHELINE_ALIGN Slot final
    {
        T value;
    };

    Slot buffers[2];

    SYS_CACHELINE_ALIGN std::atomic<uint32_t> published{0};

    void read(T &out) const noexcept
    {
        const uint32_t idx = published.load(std::memory_order_acquire);
        out = buffers[idx].value;
    }

    void write(const T &v) noexcept
    {
        const uint32_t cur = published.load(std::memory_order_relaxed);
        const uint32_t next = cur ^ 1u;
        buffers[next].value = v;
        published.store(next, std::memory_order_release);
    }
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T> class DoubleBufferWriter final
{
  public:
    explicit DoubleBufferWriter(DoubleBufferCore<T> &core) noexcept : core_(core) {}

    DoubleBufferWriter(const DoubleBufferWriter &) = delete;
    DoubleBufferWriter &operator=(const DoubleBufferWriter &) = delete;

    // Move = transfer of producer role (not duplication)
    DoubleBufferWriter(DoubleBufferWriter &&) noexcept = default;
    DoubleBufferWriter &operator=(DoubleBufferWriter &&) noexcept = default;

    void write(const T &v) noexcept { core_.write(v); }

  private:
    DoubleBufferCore<T> &core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T> class DoubleBufferReader final
{
  public:
    explicit DoubleBufferReader(const DoubleBufferCore<T> &core) noexcept : core_(core) {}

    DoubleBufferReader(const DoubleBufferReader &) = delete;
    DoubleBufferReader &operator=(const DoubleBufferReader &) = delete;

    DoubleBufferReader(DoubleBufferReader &&) noexcept = default;
    DoubleBufferReader &operator=(DoubleBufferReader &&) noexcept = default;

    void read(T &out) const noexcept { core_.read(out); }

    [[nodiscard]] bool try_read(T &out) const noexcept
    {
        read(out);
        return true;
    }

  private:
    const DoubleBufferCore<T> &core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T> class DoubleBuffer final
{
    static_assert(!stam::sys::kSystemTopologyIsSmp,
                  "DoubleBuffer is UP-only. In SMP builds use DoubleBufferSeqLock.");

  public:
    static constexpr uint32_t max_readers = 1u;

    DoubleBuffer() = default;

    DoubleBuffer(const DoubleBuffer &) = delete;
    DoubleBuffer &operator=(const DoubleBuffer &) = delete;

    [[nodiscard]] DoubleBufferWriter<T> writer() noexcept
    {
        bool expected = false;
        if (!issued_writer_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
        {
            assert(false && "DoubleBuffer::writer() already issued");
            std::abort();
        }
        return DoubleBufferWriter<T>(core_);
    }

    [[nodiscard]] DoubleBufferReader<T> reader() const noexcept
    {
        bool expected = false;
        if (!issued_reader_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
        {
            assert(false && "DoubleBuffer::reader() already issued");
            std::abort();
        }
        return DoubleBufferReader<T>(core_);
    }

    DoubleBufferCore<T> &core() noexcept { return core_; }
    const DoubleBufferCore<T> &core() const noexcept { return core_; }

  private:
    DoubleBufferCore<T> core_{};
    std::atomic<bool> issued_writer_{false};
    mutable std::atomic<bool> issued_reader_{false};
};

} // namespace stam::primitives
