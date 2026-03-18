#pragma once

#include "stam/stam.hpp"
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <cstddef>
#include <type_traits>
#include "stam/sys/sys_align.hpp" // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN

namespace stam::primitives
{

    /*
     * SPSCRingDropOldest — SPSC ring buffer with drop-oldest overflow policy.
     *
     * SEMANTICS:
     *  - FIFO delivery for all retained items.
     *  - When full, push() drops the oldest item (advances tail) and succeeds.
     *  - Intended for "latest-wins" streams where the newest data matters most.
     *
     * NOTE:
     *  - Unlike SPSCRing, the producer may advance tail_ to drop the oldest item.
     *  - This is safe because the producer never overwrites a slot that could be
     *    concurrently read; it writes only into the reserved empty slot.
     */

    // ============================================================================
    // Forward declarations
    // ============================================================================

    template <typename T, size_t Capacity>
    class SPSCRingDropOldestWriter;
    template <typename T, size_t Capacity>
    class SPSCRingDropOldestReader;
#ifdef STAM_TEST
    template <typename T, size_t Capacity>
    class SPSCRingDropOldestTest;
#endif

    // ============================================================================
    // Core (shared state carrier)
    // ============================================================================

    template <typename T, size_t Capacity>
    class SPSCRingDropOldestCore final
    {
    public:
        static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                      "Capacity must be a power of two and >= 2");

        static_assert(std::is_trivially_copyable_v<T>,
                      "SPSCRingDropOldest requires trivially copyable T");

        static_assert(SYS_CACHELINE_BYTES > 0,
                      "SYS_CACHELINE_BYTES must be defined by portability layer");

        // NOTE: Core is an intentional POD-like carrier of shared state.
        // Fields are public to make layout and invariants explicit and auditable.
        // Friend declarations document intent: only Writer/Reader access Core.

        friend class SPSCRingDropOldestWriter<T, Capacity>;
        friend class SPSCRingDropOldestReader<T, Capacity>;
#ifdef STAM_TEST
        friend class SPSCRingDropOldestTest<T, Capacity>;
#endif
private:
        // head_: index of the next slot to write into (producer-owned).
        // Written by producer (release), read by producer (relaxed) + consumer (acquire).
        SYS_CACHELINE_ALIGN std::atomic<size_t> head_{0};

        // tail_: index of the next slot to read from.
        // Written by consumer (release); producer may advance tail_ (release)
        // only on overflow to drop the oldest item.
        SYS_CACHELINE_ALIGN std::atomic<size_t> tail_{0};

        // Padding between tail_ and buffer_[0]:
        // Ensures buffer_[0] does not share a cache line with tail_.
        char pad_[SYS_CACHELINE_BYTES];

        SYS_CACHELINE_ALIGN T buffer_[Capacity];

        static_assert(std::atomic<size_t>::is_always_lock_free,
                      "std::atomic<size_t> must be lock-free on this platform");

        // Push an item into the ring (wait-free, bounded).
        // If full, drops the oldest item and still enqueues the new one.
        //
        // Returns true  → enqueued without dropping.
        // Returns false → exactly one oldest element was dropped to make room.
        [[nodiscard]] bool push(const T &item) noexcept
        {
            const size_t head = head_.load(std::memory_order_relaxed);
            const size_t next_head = (head + 1) & (Capacity - 1);

            const size_t tail = tail_.load(std::memory_order_acquire);
            const bool full = (next_head == tail);

            bool dropped = false;
            if (full)
            {
                const size_t next_tail = (tail + 1) & (Capacity - 1);
                // Do not "store" tail directly: consumer owns tail_ and may have advanced
                // it since our load. A stale store could roll tail_ backwards.
                //
                // Single CAS attempt keeps push() O(1) (no loops). If CAS fails, it means
                // the consumer advanced tail_ and the ring is no longer full.
                size_t expected = tail;
                dropped = tail_.compare_exchange_strong(expected, next_tail,
                                                              std::memory_order_release,
                                                              std::memory_order_relaxed);
            }

            buffer_[head] = item;
            head_.store(next_head, std::memory_order_release);
            return !dropped;
        }

        // Approximate occupancy — telemetry only.
        // May return stale values; must not be used for flow control or sync.
        [[nodiscard]] bool full() const noexcept
        {
            const size_t head = head_.load(std::memory_order_relaxed);
            const size_t next_head = (head + 1) & (Capacity - 1);
            return next_head == tail_.load(std::memory_order_relaxed);
        }
        
        // Pop an item from the ring (wait-free, bounded).
        // Returns true on success, false if the ring is empty.
        [[nodiscard]] bool pop(T &item) noexcept
        {
            const size_t tail = tail_.load(std::memory_order_relaxed);

            if (tail == head_.load(std::memory_order_acquire))
            {
                return false; // ring is empty
            }

            item = buffer_[tail];
            tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
            return true;
        }

        // Approximate occupancy — telemetry only.
        // May return stale values; must not be used for flow control or sync.
        [[nodiscard]] bool empty() const noexcept
        {
            return tail_.load(std::memory_order_relaxed) ==
                   head_.load(std::memory_order_relaxed);
        }

    };

    // ============================================================================
    // Producer view
    // ============================================================================

    template <typename T, size_t Capacity>
    class SPSCRingDropOldestWriter final
    {
    public:
        explicit SPSCRingDropOldestWriter(SPSCRingDropOldestCore<T, Capacity> &core) noexcept
            : core_(core) {}

        SPSCRingDropOldestWriter(const SPSCRingDropOldestWriter &) = delete;
        SPSCRingDropOldestWriter &operator=(const SPSCRingDropOldestWriter &) = delete;

        // Move = transfer of producer role (not duplication).
        SPSCRingDropOldestWriter(SPSCRingDropOldestWriter &&) noexcept = default;
        SPSCRingDropOldestWriter &operator=(SPSCRingDropOldestWriter &&) noexcept = default;

        // Push an item into the ring (wait-free, bounded).
        // If full, drops the oldest item and still enqueues the new one.
        //
        // Returns true  → enqueued without dropping.
        // Returns false → exactly one oldest element was dropped to make room.
        [[nodiscard]] bool push(const T &item) noexcept
        {
            return core_.push(item);
        }

        // Approximate occupancy — telemetry only.
        // May return stale values; must not be used for flow control or sync.
        [[nodiscard]] bool full() const noexcept
        {
            return core_.full();
        }

        static constexpr size_t usable_capacity() noexcept { return Capacity - 1; }

    private:
        SPSCRingDropOldestCore<T, Capacity> &core_;
    };

    // ============================================================================
    // Consumer view
    // ============================================================================

    template <typename T, size_t Capacity>
    class SPSCRingDropOldestReader final
    {
    public:
        explicit SPSCRingDropOldestReader(SPSCRingDropOldestCore<T, Capacity> &core) noexcept
            : core_(core) {}

        SPSCRingDropOldestReader(const SPSCRingDropOldestReader &) = delete;
        SPSCRingDropOldestReader &operator=(const SPSCRingDropOldestReader &) = delete;

        // Move = transfer of consumer role (not duplication).
        SPSCRingDropOldestReader(SPSCRingDropOldestReader &&) noexcept = default;
        SPSCRingDropOldestReader &operator=(SPSCRingDropOldestReader &&) noexcept = default;

        // Pop an item from the ring (wait-free, bounded).
        // Returns true on success, false if the ring is empty.
        [[nodiscard]] bool pop(T &item) noexcept
        {
            return core_.pop(item);
        }

        // Approximate occupancy — telemetry only.
        // May return stale values; must not be used for flow control or sync.
        [[nodiscard]] bool empty() const noexcept
        {
            return core_.empty();
        }

        static constexpr size_t usable_capacity() noexcept { return Capacity - 1; }

    private:
        SPSCRingDropOldestCore<T, Capacity> &core_;
    };

    // ============================================================================
    // Convenience wrapper
    // ============================================================================

    template <typename T, size_t Capacity>
    class SPSCRingDropOldest final
    {
    public:
        static constexpr size_t max_readers = 1;

        SPSCRingDropOldest() = default;

        SPSCRingDropOldest(const SPSCRingDropOldest &) = delete;
        SPSCRingDropOldest &operator=(const SPSCRingDropOldest &) = delete;

        [[nodiscard]] SPSCRingDropOldestWriter<T, Capacity> writer() noexcept
        {
            bool expected = false;
            if (!issued_writer_.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
            {
                assert(false && "SPSCRingDropOldest::writer() already issued");
                std::abort();
            }
            return SPSCRingDropOldestWriter<T, Capacity>(core_);
        }

        [[nodiscard]] SPSCRingDropOldestReader<T, Capacity> reader() noexcept
        {
            bool expected = false;
            if (!issued_reader_.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
            {
                assert(false && "SPSCRingDropOldest::reader() already issued");
                std::abort();
            }
            return SPSCRingDropOldestReader<T, Capacity>(core_);
        }

        SPSCRingDropOldestCore<T, Capacity> &core() noexcept { return core_; }
        const SPSCRingDropOldestCore<T, Capacity> &core() const noexcept { return core_; }

    private:
        SPSCRingDropOldestCore<T, Capacity> core_{};
        std::atomic<bool> issued_writer_{false};
        std::atomic<bool> issued_reader_{false};
    };

} // namespace stam::primitives
