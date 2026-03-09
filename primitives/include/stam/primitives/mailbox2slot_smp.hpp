#pragma once

#include "stam/stam.hpp"
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "stam/sys/sys_align.hpp"     // SYS_CACHELINE_BYTES

namespace stam::primitives {

/*
 * Mailbox2SlotSmp<T> — SPSC snapshot mailbox, SMP-safe (latest-wins).
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and exactly 1 consumer (reader)
 *  - writer: write-only; reader: read-only
 *  - writer is NOT re-entrant (no nested IRQ/NMI calling publish())
 *  - reader is NOT re-entrant
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *
 * PLATFORM CONSTRAINT:
 *  - Default/active profile: platform-optimized.
 *  - SMP-safe in platform-optimized profile. No preemption_disable needed.
 *    UP-only predecessor: Mailbox2Slot.
 *  - strict ISO C++ profile requires a race-free payload design
 *    (different implementation).
 *  - NOTE: strict profile is currently NOT implemented in this class.
 *
 * SEMANTICS:
 *  - Snapshot / latest-wins. Intermediate updates may be lost.
 *  - try_read() returns false if no data published yet, or a concurrent write
 *    is detected during copy (single-shot: no internal retry).
 *  - Reader uses sticky-state strategy: on false, caller retains prior value.
 *
 * PROTOCOL (2 slots + per-slot sequence counters):
 *  - Writer always writes to the slot that is NOT currently published
 *    (published ^ 1). To cause a reader collision, writer must complete
 *    2 full publish cycles — low false-return rate in RT polling loops.
 *  - Per-slot seq: even = quiescent, odd = write in progress.
 *    Reader accepts data only if same even seq observed before and after copy.
 *  - This protects snapshot acceptance at protocol level; it does not convert
 *    overlapping payload reads/writes into strict ISO C++ race-free semantics.
 *
 * PROGRESS:
 *  - publish(): wait-free, O(1). 2 atomic RMW + payload copy + 2 stores.
 *  - try_read(): wait-free per invocation (single-shot), O(1).
 *               1–2 atomic loads + payload copy + 1 re-verify load; no RMW
 *               on fast successful path.
 *
 * MISUSE GUARDS:
 *  - writer() may be issued at most once per primitive lifetime.
 *  - reader() may be issued at most once per primitive lifetime.
 *  - Exceeding either limit triggers fail-fast (assert + abort).
 *
 * SPEC: primitives/docs/Mailbox2SlotSmp - RT Contract & Invariants.md (Rev 1.1)
 */

template <typename T> class Mailbox2SlotSmpWriter;
template <typename T> class Mailbox2SlotSmpReader;

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T>
struct Mailbox2SlotSmpCore final {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Mailbox2SlotSmp requires trivially copyable T");
    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by portability layer");
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "std::atomic<uint32_t> must be lock-free on this platform");
    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be lock-free on this platform");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "std::atomic<bool> must be lock-free on this platform");

    friend class Mailbox2SlotSmpWriter<T>;
    friend class Mailbox2SlotSmpReader<T>;

    // ---- Data slots --------------------------------------------------------

    // Each slot occupies an integer number of cachelines to prevent false
    // sharing between writer filling one slot and reader copying the other.
    struct alignas(SYS_CACHELINE_BYTES) Slot final {
        T value;
    };
    static_assert(sizeof(Slot) % SYS_CACHELINE_BYTES == 0,
                  "Slot must occupy an integer number of cachelines; "
                  "consider padding T or using a wrapper");

    Slot slots[2];

    // ---- Per-slot sequence counters ----------------------------------------

    // seq[i]: even = quiescent (slot i is stable), odd = write in progress.
    // Writer opens seq[j] (odd) before writing slots[j], closes (even) after.
    // Reader verifies same even seq[i] before and after copying slots[i].
    //
    // Each counter on its own cacheline to avoid false sharing between
    // writer updating seq[j] and reader verifying seq[i] (j != i).
    struct alignas(SYS_CACHELINE_BYTES) SeqLine final {
        std::atomic<uint32_t> seq{0};
    };
    SeqLine seqs[2];

    // ---- Control block -----------------------------------------------------

    // published: index of the currently published slot ({0, 1}).
    //   Single-writer (only writer stores); reader loads (acquire).
    // has_value: false until the first publish(); thereafter always true.
    //   Provides a clean "no data yet" sentinel without reserved slot index.
    //
    // Both fields together on one cacheline: accessed on every op by both sides.
    struct alignas(SYS_CACHELINE_BYTES) Control final {
        std::atomic<uint8_t> published{0};
        std::atomic<bool>    has_value{false};
    };
    Control ctrl;

    Mailbox2SlotSmpCore() noexcept = default;

    Mailbox2SlotSmpCore(const Mailbox2SlotSmpCore&)            = delete;
    Mailbox2SlotSmpCore& operator=(const Mailbox2SlotSmpCore&) = delete;
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T>
class Mailbox2SlotSmpWriter final {
public:
    explicit Mailbox2SlotSmpWriter(Mailbox2SlotSmpCore<T>& core) noexcept
        : core_(core) {}

    Mailbox2SlotSmpWriter(const Mailbox2SlotSmpWriter&)            = delete;
    Mailbox2SlotSmpWriter& operator=(const Mailbox2SlotSmpWriter&) = delete;

    // Move = transfer of producer role (not duplication).
    Mailbox2SlotSmpWriter(Mailbox2SlotSmpWriter&&) noexcept            = default;
    Mailbox2SlotSmpWriter& operator=(Mailbox2SlotSmpWriter&&) noexcept = default;

    // Publish a new snapshot (wait-free, O(1)).
    //
    // Protocol:
    //   Step 1: j = published ^ 1. Write to the slot that is NOT published.
    //           Writer never touches the published slot (torn-read exclusion).
    //   Step 2: seq[j].fetch_add(1, release) → odd. Marks slot j as being written.
    //           Any reader that loads seq[j] odd will reject this slot.
    //   Step 3: write slots[j].value (non-atomic; single writer, no WW race).
    //   Step 4: seq[j].fetch_add(1, release) → even. Closes write; establishes
    //           happens-before with reader's load(acquire) on matching seq.
    //   Step 5: published.store(j, release). Makes slot j the new publication.
    //   Step 6: has_value.store(true, release). Idempotent after first call.
    void publish(const T& value) noexcept {
        // Step 1: select the non-published slot.
        // relaxed: writer is the sole writer of published; read-my-own-write.
        const uint8_t pub = core_.ctrl.published.load(std::memory_order_relaxed);
        const uint8_t j   = pub ^ 1u;

        // Step 2: open write window on slot j (seq → odd).
        core_.seqs[j].seq.fetch_add(1u, std::memory_order_release);

        // Step 3: write payload (non-atomic).
        // Safety here is protocol-level in platform-optimized profile:
        // reader rejects overlapping copies using seq re-verify.
        core_.slots[j].value = value;

        // Step 4: close write window (seq → even).
        core_.seqs[j].seq.fetch_add(1u, std::memory_order_release);

        // Step 5: atomically switch publication.
        core_.ctrl.published.store(j, std::memory_order_release);

        // Step 6: signal that data is available (idempotent after first call).
        core_.ctrl.has_value.store(true, std::memory_order_release);
    }

    // Unified snapshot API alias.
    void write(const T& value) noexcept {
        publish(value);
    }

private:
    Mailbox2SlotSmpCore<T>& core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T>
class Mailbox2SlotSmpReader final {
public:
    explicit Mailbox2SlotSmpReader(Mailbox2SlotSmpCore<T>& core) noexcept
        : core_(core) {}

    Mailbox2SlotSmpReader(const Mailbox2SlotSmpReader&)            = delete;
    Mailbox2SlotSmpReader& operator=(const Mailbox2SlotSmpReader&) = delete;

    // Move = transfer of consumer role (not duplication).
    Mailbox2SlotSmpReader(Mailbox2SlotSmpReader&&) noexcept            = default;
    Mailbox2SlotSmpReader& operator=(Mailbox2SlotSmpReader&&) noexcept = default;

    // Try to read the latest published snapshot (wait-free per invocation, O(1)).
    //
    // Returns false → no data published yet (has_value == false), or
    //                 concurrent write detected (seq changed during copy).
    //                 Caller retains prior state (sticky-state strategy).
    // Returns true  → out contains a consistent snapshot of the latest state.
    //
    // Protocol (single-shot):
    //   Step 1: has_value.load(acquire). If false → return false.
    //   Step 2: i = published.load(acquire). Current published slot index.
    //   Step 3: s1 = seq[i].load(acquire).
    //           If odd: writer is actively updating slot i → return false.
    //           (Defensive guard; in steady state published slot has even seq.)
    //   Step 4: copy slots[i].value into out.
    //   Step 5: s2 = seq[i].load(acquire). If s1 != s2: write overlapped → false.
    //           Same even seq before and after copy → snapshot is stable.
    //   Step 6: return true.
    //
    // Invariant: in steady state, for a slot loaded via published.load(acquire),
    // seq[published] is even. The odd-check in step 3 is a defensive guard
    // against transient race visibility edge cases.
    [[nodiscard]] bool try_read(T& out) noexcept {
        // Step 1: no data published yet.
        if (!core_.ctrl.has_value.load(std::memory_order_acquire)) {
            return false;
        }

        // Step 2: load published slot index.
        const uint8_t i = core_.ctrl.published.load(std::memory_order_acquire);

        // Step 3: load seq and check for active write (defensive odd-check).
        const uint32_t s1 = core_.seqs[i].seq.load(std::memory_order_acquire);
        if (s1 & 1u) {
            return false;  // writer is updating slot i
        }

        // Step 4: copy payload.
        out = core_.slots[i].value;

        // Step 5: re-verify seq. If changed, a write overlapped our copy.
        const uint32_t s2 = core_.seqs[i].seq.load(std::memory_order_acquire);
        if (s1 != s2) {
            return false;  // torn copy — discard
        }

        // Step 6: snapshot is stable.
        return true;
    }

private:
    Mailbox2SlotSmpCore<T>& core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T>
class Mailbox2SlotSmp final {
public:
    static constexpr uint32_t max_readers = 1u;

    Mailbox2SlotSmp() = default;

    Mailbox2SlotSmp(const Mailbox2SlotSmp&)            = delete;
    Mailbox2SlotSmp& operator=(const Mailbox2SlotSmp&) = delete;

    [[nodiscard]] Mailbox2SlotSmpWriter<T> writer() noexcept {
        bool expected = false;
        if (!issued_writer_.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            assert(false && "Mailbox2SlotSmp::writer() already issued");
            std::abort();
        }
        return Mailbox2SlotSmpWriter<T>(core_);
    }

    [[nodiscard]] Mailbox2SlotSmpReader<T> reader() noexcept {
        bool expected = false;
        if (!issued_reader_.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            assert(false && "Mailbox2SlotSmp::reader() already issued");
            std::abort();
        }
        return Mailbox2SlotSmpReader<T>(core_);
    }

    Mailbox2SlotSmpCore<T>&       core() noexcept       { return core_; }
    const Mailbox2SlotSmpCore<T>& core() const noexcept { return core_; }

private:
    Mailbox2SlotSmpCore<T> core_;
    std::atomic<bool>      issued_writer_{false};
    std::atomic<bool>      issued_reader_{false};
};

} // namespace stam::primitives
