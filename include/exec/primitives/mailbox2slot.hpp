#pragma once

#include "stam/stam.hpp"
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "sys/sys_align.hpp"   // SYS_CACHELINE_BYTES, SYS_CACHELINE_ALIGN

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
 *  - publish(): wait-free, O(1), bounded atomic ops, no loops/CAS/mutex
 *  - try_read(): wait-free, O(1), bounded atomic ops + copy(T)
 *
 * MISUSE:
 *  - Violations of the above contract result in undefined behavior
 *    with respect to the intended semantics of this component.
 *
 * SPEC: docs/contracts/Mailbox2Slot.md (Revision 1.3)
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

    struct Slot final {
        // Each slot on its own cache line to avoid false sharing between
        // writer filling one slot and reader copying from the other.
        SYS_CACHELINE_ALIGN T value;
    };

    Slot slots[2];

    // pub_state: which slot is currently published (or NONE).
    // Written only by writer (release), read by both sides (acquire).
    //
    // Placed on its own cache line: writer modifies pub_state on every
    // publish(); reader loads it on every try_read(). Separating from
    // lock_state avoids writer↔reader false sharing on the control word.
    SYS_CACHELINE_ALIGN std::atomic<uint8_t> pub_state{kNone};

    // lock_state: which slot the reader currently holds (or UNLOCKED).
    // Written only by reader (release), read by writer (acquire).
    //
    // Separated from pub_state: writer reads lock_state once per publish()
    // to select a slot; reader writes it twice per try_read(). Keeping them
    // on distinct cache lines prevents unnecessary invalidations.
    SYS_CACHELINE_ALIGN std::atomic<uint8_t> lock_state{kUnlocked};

    static_assert(std::atomic<uint8_t>::is_always_lock_free,
                  "std::atomic<uint8_t> must be lock-free on this platform");
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
    // Slot selection (I3, Lemma: Safe Slot Availability):
    //   Read lock_state once with acquire. By the SPSC contract the reader
    //   holds at most one slot at a time, so the other slot is always free.
    //   acquire ensures happens-before with reader's release-store of
    //   lock_state, which is the foundation of the ABA proof (see spec I6).
    //
    // Invalidate path (I5):
    //   If the chosen slot j is already published, we must invalidate
    //   pub_state before writing to prevent reader from starting a claim
    //   on a slot being overwritten. No race with reader here: we selected
    //   j != lock_state, so reader cannot claim j at this point.
    void publish(const T& value) noexcept {
        // Step 1: choose slot j != lock_state.
        const uint8_t locked = core_.lock_state.load(std::memory_order_acquire);
        const uint8_t j      = (locked == kSlot1) ? kSlot0 : kSlot1;

        // Step 2: invalidate if j is currently published (I5).
        if (core_.pub_state.load(std::memory_order_acquire) == j) {
            core_.pub_state.store(kNone, std::memory_order_release);
        }

        // Step 3: write data, then publish.
        core_.slots[j].value = value;
        core_.pub_state.store(j, std::memory_order_release);
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
    // Claim-verify protocol (I6):
    //   p1 = load pub_state (acquire)
    //   lock slot p1         (release) ← visible to writer's acquire-load
    //   p2 = load pub_state (acquire)
    //   if p1 == p2: slot is stable, safe to copy
    //   else:        writer changed pub_state between our loads → abort
    //
    // ABA safety: if writer wanted to re-publish slot p1, it must write
    // to S[p1]. But our lock_state.store(p1, release) + writer's
    // lock_state.load(acquire) establishes happens-before, so writer sees
    // lock_state == p1 and is forbidden from writing to S[p1] by I3.
    [[nodiscard]] bool try_read(T& out) noexcept {
        // Step 1: load published slot index.
        const uint8_t p1 = core_.pub_state.load(std::memory_order_acquire);

        // Step 2: nothing published yet (or between publications).
        // lock_state is already UNLOCKED by postcondition of previous call.
        if (p1 == kNone) {
            return false;
        }

        // Step 3: claim slot p1.
        core_.lock_state.store(p1, std::memory_order_release);

        // Step 4: verify publication has not changed since step 1.
        const uint8_t p2 = core_.pub_state.load(std::memory_order_acquire);

        if (p2 != p1) {
            // Writer published something new between steps 1 and 4.
            // Release the claim and signal miss.
            core_.lock_state.store(kUnlocked, std::memory_order_release);
            return false;
        }

        // Step 5: safe to copy — slot p1 is stable.
        out = core_.slots[p1].value;

        // Step 6: release claim. Postcondition: lock_state == UNLOCKED.
        core_.lock_state.store(kUnlocked, std::memory_order_release);
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

    // NOTE: Creating more than one Writer or Reader for the same mailbox
    // violates the 1P/1C contract and is semantically undefined.
    // Runtime guards are intentionally omitted to keep the RT path minimal.

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
