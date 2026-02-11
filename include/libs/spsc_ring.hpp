#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>
#include "sys/sys_align.hpp"

namespace libs {

// Single-Producer Single-Consumer lock-free ring buffer
// Preallocated, fixed capacity, no runtime allocation
template <typename T, size_t Capacity>
class SPSCRing {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
              "Capacity must be power of two and >= 2");
  static_assert(std::is_trivially_copyable_v<T>,
              "T must be trivially copyable for RT safety");

public:
  SPSCRing() noexcept : head_(0), tail_(0) {}

  // Producer side (can be called from RT context)
  [[nodiscard]] bool push(const T& item) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & (Capacity - 1);
    
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false; // Full
    }
    
    buffer_[head] = item;
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  // Consumer side (typically non-RT context)
  [[nodiscard]] bool pop(T& item) noexcept {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    
    if (tail == head_.load(std::memory_order_acquire)) {
      return false; // Empty
    }
    
    item = buffer_[tail];
    tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
    return true;
  }

  // Approximate; not linearizable. Telemetry only.
  // May return stale values; must not be used for synchronization.
  bool empty() const noexcept {
    return tail_.load(std::memory_order_relaxed) == 
           head_.load(std::memory_order_relaxed);
  }

  // Approximate; not linearizable. Telemetry only
  // May return stale values; must not be used for synchronization.
  bool full() const noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & (Capacity - 1);
    return next_head == tail_.load(std::memory_order_relaxed);
  }

  static constexpr size_t usable_capacity() noexcept { return Capacity - 1; }

private:
  // Separate cache lines for head/tail to avoid false sharing
  alignas(SYS_CACHELINE_BYTES) std::atomic<size_t> head_;
  alignas(SYS_CACHELINE_BYTES) std::atomic<size_t> tail_;
  char pad[SYS_CACHELINE_BYTES];
  alignas(SYS_CACHELINE_BYTES) T buffer_[Capacity];
};

} // namespace libs