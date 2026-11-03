#include "photoplus/memory.hpp"

#include <cstdlib>
#include <new>

namespace photoplus {

MemoryTracker& MemoryTracker::instance() {
  static MemoryTracker tracker;
  return tracker;
}

void* MemoryTracker::allocate(std::size_t bytes, pp_status* status) {
  if (bytes == 0) {
    if (status) *status = PP_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }

  // Reserve against the budget first using CAS so concurrent jobs cannot
  // jointly overshoot it.
  const std::uint64_t budget = budget_.load(std::memory_order_relaxed);
  std::uint64_t current = current_.load(std::memory_order_relaxed);
  std::uint64_t next = 0;
  do {
    next = current + bytes;
    if (budget != 0 && next > budget) {
      failed_.fetch_add(1, std::memory_order_relaxed);
      if (status) *status = PP_ERROR_BUDGET_EXCEEDED;
      return nullptr;
    }
  } while (!current_.compare_exchange_weak(current, next, std::memory_order_relaxed));

  // aligned_alloc requires the size to be a multiple of the alignment.
  const std::size_t rounded = (bytes + kAlignment - 1) / kAlignment * kAlignment;
  void* ptr = std::aligned_alloc(kAlignment, rounded);
  if (ptr == nullptr) {
    current_.fetch_sub(bytes, std::memory_order_relaxed);
    failed_.fetch_add(1, std::memory_order_relaxed);
    if (status) *status = PP_ERROR_OUT_OF_MEMORY;
    return nullptr;
  }

  std::uint64_t peak = peak_.load(std::memory_order_relaxed);
  while (next > peak && !peak_.compare_exchange_weak(peak, next, std::memory_order_relaxed)) {
  }
  live_.fetch_add(1, std::memory_order_relaxed);
  total_.fetch_add(1, std::memory_order_relaxed);
  if (status) *status = PP_OK;
  return ptr;
}

void MemoryTracker::release(void* ptr, std::size_t bytes) noexcept {
  if (ptr == nullptr) return;
  std::free(ptr);
  current_.fetch_sub(bytes, std::memory_order_relaxed);
  live_.fetch_sub(1, std::memory_order_relaxed);
}

void MemoryTracker::resetPeak() noexcept {
  peak_.store(current_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

pp_memory_stats MemoryTracker::stats() const noexcept {
  pp_memory_stats s{};
  s.current_bytes = current_.load(std::memory_order_relaxed);
  s.peak_bytes = peak_.load(std::memory_order_relaxed);
  s.budget_bytes = budget_.load(std::memory_order_relaxed);
  s.live_allocations = live_.load(std::memory_order_relaxed);
  s.total_allocations = total_.load(std::memory_order_relaxed);
  s.failed_allocations = failed_.load(std::memory_order_relaxed);
  return s;
}

}  // namespace photoplus
