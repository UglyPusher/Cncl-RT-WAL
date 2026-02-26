#pragma once

#include "stam/stam.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "sys/sys_align.hpp"       // SYS_CACHELINE_BYTES
#include "sys/sys_preemption.hpp"  // sys_preemption_disable(), sys_preemption_enable()

namespace stam::exec::primitives {

/*
 * Mailbox2Slot — SPSC snapshot mailbox (latest-wins, reader-claim).
 *
 * CONTRACT (hard requirements):
 *  - exactly 1 producer (writer) and exactly 1 consumer (reader)
 *  - producer: write-only; consumer: read-only
 *  - producer is NOT re-entrant (no nested IRQ/NMI calling publish())
 *  - consumer is NOT re-entrant
 *  - T is trivially copyable (bounded, deterministic copy; no ctor/dtor)
 *
 * SEMANTICS:
 *  - Snapshot / frame primitive, NOT a queue/log.
 *  - Intermediate updates may be lost (latest-wins).
 *  - try_read() returns false if no data is available or a publication
 *    race is detected; reader stays with its previous (sticky) state.
 *  - No retry: on false return the reader skips to the next tick.
 *
 * POSTCONDITION OF try_read():
 *  - lock_state == UNLOCKED regardless of return value.
 *
 * RT APPLICABILITY:
 *  - publish(): wait-free, O(1). Preemption disabled only for slot
 *    selection + invalidate (atomic ops only, independent of sizeof(T)).
 *  - try_read(): wait-free, O(1). Preemption disabled only for
 *    claim-verify (3 atomic ops, independent of sizeof(T)).
 *  - copy(T) is outside both critical sections.
 *
 * PREEMPTION SAFETY (uniprocessor):
 *  - Both publish() and try_read() use sys_preemption_disable/enable to
 *    protect their respective atomic windows. WCET of each critical
 *    section is independent of sizeof(T).
 *  - On SMP: preemption disable is insufficient; torn reads are possible.
 *    See Known Limitations in spec (Revision 1.4).
 *
 * MISUSE:
 *  - Violations of the above contract result in undefined behavior.
 *  - Mailbox2Slot::writer() / reader() may be called multiple times, each
 *    returning a new handle to the same Core. Creating more than one Writer
 *    or more than one Reader for the same Core violates the 1P/1C contract.
 *    No runtime guard is provided to keep the RT path minimal.
 *
 * SPEC: docs/contracts/Mailbox2Slot.md (Revision 1.4)
 */

// ============================================================================
// State encoding
// ============================================================================

// pub_state  : 0 = slot 0 published, 1 = slot 1 published, 2 = NONE
// lock_state : 0 = slot 0 locked,    1 = slot 1 locked,    2 = UNLOCKED
inline constexpr uint8_t kSlot0    = 0u;
inline constexpr uint8_t kSlot1    = 1u;
inline constexpr uint8_t kNone     = 2u;   // pub_state: nothing published
inline constexpr uint8_t kUnlocked = 2u;   // lock_state: reader holds no slot

// ============================================================================
// CachelinePadded — wraps an atomic in a full cacheline to eliminate
// false sharing. Padding is verified at compile time.
// ============================================================================

template <typename A>
struct alignas(SYS_CACHELINE_BYTES) CachelinePadded final {
    static_assert(sizeof(A) <= SYS_CACHELINE_BYTES,
                  "CachelinePadded: atomic wider than cacheline");

    A value;

private:
    std::byte pad_[SYS_CACHELINE_BYTES - sizeof(A)];
};

static_assert(sizeof(CachelinePadded<std::atomic<uint8_t>>) == SYS_CACHELINE_BYTES,
              "CachelinePadded must be exactly one cacheline");

// ============================================================================
// Forward declarations
// ============================================================================

template <typename T> class Mailbox2SlotWriter;
template <typename T> class Mailbox2SlotReader;

// ============================================================================
// Core (shared state carrier)
// ============================================================================

template <typename T>
struct Mailbox2SlotCore final {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Mailbox2Slot requires trivially copyable T");

    static_assert(SYS_CACHELINE_BYTES > 0,
                  "SYS_CACHELINE_BYTES must be defined by portability layer");

    // NOTE: Core is an intentional POD-like carrier of shared state.
    // Fields are public to make layout and invariants explicit and auditable.
    // Friend declarations document intent: only Writer/Reader access Core.

    friend class Mailbox2SlotWriter<T>;
    friend class Mailbox2SlotReader<T>;

    // Each slot occupies an integer number of cachelines to prevent
    // false sharing between writer filling one slot and reader copying
    // from the other, and between slots[1] tail and pub_state.
    struct alignas(SYS_CACHELINE_BYTES) Slot final {
        T value;
    };
    static_assert(sizeof(Slot) % SYS_CACHELINE_BYTES == 0,
                  "Slot must occupy an integer number of cachelines; "
                  "consider padding T or using a wrapper");

    Slot slots[2];

    // pub_state: which slot is currently published (or NONE).
    // Single-writer (only writer stores); read by both sides (acquire).
    // Padded to a full cacheline: writer modifies on every publish(),
    // reader loads on every try_read(); separating from lock_state
    // eliminates writer↔reader false sharing on the control word.
    CachelinePadded<std::atomic<uint8_t>> pub_state{};   // .value = kNone

    // lock_state: which slot the reader currently holds (or UNLOCKED).
    // Single-writer (only reader stores); read by writer (acquire).
    // Padded to a full cacheline: writer reads once per publish(),
    // reader writes twice per try_read(); separate line prevents
    // unnecessary cache invalidations on the peer core.
    CachelinePadded<std::atomic<uint8_t>> lock_state{};  // .value = kUnlocked

    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be lock-free on this platform");

    // Verify overall layout: no field bleeds into a neighbour's cacheline.
    static_assert(sizeof(Mailbox2SlotCore) ==
                      2 * sizeof(Slot) +
                      sizeof(CachelinePadded<std::atomic<uint8_t>>) * 2,
                  "Mailbox2SlotCore layout unexpected; check padding");
};

// ============================================================================
// Producer view
// ============================================================================

template <typename T>
class Mailbox2SlotWriter final {
public:
    explicit Mailbox2SlotWriter(Mailbox2SlotCore<T>& core) noexcept
        : core_(core) {}

    Mailbox2SlotWriter(const Mailbox2SlotWriter&)            = delete;
    Mailbox2SlotWriter& operator=(const Mailbox2SlotWriter&) = delete;

    // Move = transfer of producer role (not duplication).
    Mailbox2SlotWriter(Mailbox2SlotWriter&&) noexcept            = default;
    Mailbox2SlotWriter& operator=(Mailbox2SlotWriter&&) noexcept = default;

    // Publish a new snapshot (wait-free, bounded).
    //
    // Critical section (preemption disabled):
    //   Covers slot selection and conditional invalidate only.
    //   Does NOT include copy(T); WCET is independent of sizeof(T).
    //
    //   Step 1 — choose slot j != lock_state:
    //     load(acquire) establishes happens-before with reader's
    //     lock_state.store(release); writer sees the slot currently
    //     held by reader (I3).
    //
    //   Step 2 — conditional invalidate (I5):
    //     If pub_state == j, store NONE before writing data.
    //     load(relaxed): writer is the single-writer of pub_state;
    //     this is read-my-own-write; no additional ordering needed.
    //     No race with reader: pub_state == j implies lock_state != j
    //     (writer would have chosen the other slot), so reader cannot
    //     start a claim on j at this point.
    //
    //   Invariant on preemption_enable():
    //     pub_state != j  OR  pub_state == NONE.
    //     Any reader arriving after this point cannot claim slot j;
    //     copy(T) is therefore safe without preemption guard.
    //
    // Outside critical section:
    //   Step 3 — write data into S[j], then publish j.
    //   ABA safety: see I6 in spec.
    void publish(const T& value) noexcept {
        // --- critical section: slot selection + invalidate ---
        sys_preemption_disable();

        const uint8_t locked = core_.lock_state.value.load(std::memory_order_acquire);
        const uint8_t j      = (locked == kSlot1) ? kSlot0 : kSlot1;

        if (core_.pub_state.value.load(std::memory_order_relaxed) == j) {
            core_.pub_state.value.store(kNone, std::memory_order_release);
        }

        sys_preemption_enable();
        // --- end critical section ---
        // Invariant: slot j is unreachable by reader until pub_state.store(j).

        core_.slots[j].value = value;
        core_.pub_state.value.store(j, std::memory_order_release);
    }

private:
    Mailbox2SlotCore<T>& core_;
};

// ============================================================================
// Consumer view
// ============================================================================

template <typename T>
class Mailbox2SlotReader final {
public:
    explicit Mailbox2SlotReader(Mailbox2SlotCore<T>& core) noexcept
        : core_(core) {}

    Mailbox2SlotReader(const Mailbox2SlotReader&)            = delete;
    Mailbox2SlotReader& operator=(const Mailbox2SlotReader&) = delete;

    // Move = transfer of consumer role (not duplication).
    Mailbox2SlotReader(Mailbox2SlotReader&&) noexcept            = default;
    Mailbox2SlotReader& operator=(Mailbox2SlotReader&&) noexcept = default;

    // Try to read the latest published snapshot (wait-free, bounded).
    //
    // Returns true  → out contains a consistent snapshot.
    // Returns false → no data available or publication race detected;
    //                 out is unchanged; reader should use previous state.
    //
    // POSTCONDITION: lock_state == UNLOCKED regardless of return value.
    //
    // Critical section (preemption disabled):
    //   Covers claim-verify only (steps 1–4): 3 atomic ops, independent
    //   of sizeof(T). Eliminates false returns caused by writer preempting
    //   reader between p1-load and p2-verify. copy(T) is outside —
    //   symmetric with writer's design.
    //
    // Claim-verify protocol (I6):
    //   p1 = load pub_state (acquire)
    //   lock slot p1         (release) ← visible to writer's acquire-load
    //   p2 = load pub_state (acquire)
    //   if p1 == p2: slot is stable, safe to copy
    //   else:        writer changed pub_state between our loads → abort
    //
    // ABA safety: lock_state.store(p1, release) + writer's
    // lock_state.load(acquire) establishes happens-before; writer sees
    // lock_state == p1 and is forbidden from writing to S[p1] by I3.
    [[nodiscard]] bool try_read(T& out) noexcept {
        // --- critical section: claim-verify ---
        sys_preemption_disable();

        // Step 1: load published slot index.
        const uint8_t p1 = core_.pub_state.value.load(std::memory_order_acquire);

        // Step 2: nothing published yet (or between publications).
        // lock_state is already UNLOCKED by postcondition of previous call.
        if (p1 == kNone) {
            sys_preemption_enable();
            return false;
        }

        // Step 3: claim slot p1.
        core_.lock_state.value.store(p1, std::memory_order_release);

        // Step 4: verify publication has not changed since step 1.
        const uint8_t p2 = core_.pub_state.value.load(std::memory_order_acquire);

        sys_preemption_enable();
        // --- end critical section ---

        if (p2 != p1) {
            // Writer changed pub_state between steps 1 and 4.
            // Release the claim and signal miss.
            core_.lock_state.value.store(kUnlocked, std::memory_order_release);
            return false;
        }

        // Step 5: safe to copy — slot p1 is stable.
        out = core_.slots[p1].value;

        // Step 6: release claim. Postcondition: lock_state == UNLOCKED.
        core_.lock_state.value.store(kUnlocked, std::memory_order_release);
        return true;
    }

private:
    Mailbox2SlotCore<T>& core_;
};

// ============================================================================
// Convenience wrapper
// ============================================================================

template <typename T>
class Mailbox2Slot final {
public:
    Mailbox2Slot() = default;

    Mailbox2Slot(const Mailbox2Slot&)            = delete;
    Mailbox2Slot& operator=(const Mailbox2Slot&) = delete;

    // NOTE: writer() and reader() each return a new handle to the shared Core.
    // Calling writer() more than once yields two Writer objects for the same
    // Core — this violates the 1P/1C contract and results in undefined
    // behavior. The same applies to reader(). No runtime guard is provided;
    // enforcement is the caller's responsibility.

    [[nodiscard]] Mailbox2SlotWriter<T> writer() noexcept {
        return Mailbox2SlotWriter<T>(core_);
    }

    [[nodiscard]] Mailbox2SlotReader<T> reader() noexcept {
        return Mailbox2SlotReader<T>(core_);
    }

    Mailbox2SlotCore<T>&       core() noexcept       { return core_; }
    const Mailbox2SlotCore<T>& core() const noexcept { return core_; }

private:
    Mailbox2SlotCore<T> core_{};
};

} // namespace stam::exec::primitives
