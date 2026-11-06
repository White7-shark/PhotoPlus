#include "photoplus/memory.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <new>

namespace photoplus {

namespace {

std::size_t roundUp(std::size_t bytes) {
  return (bytes + MemoryTracker::kAlignment - 1) / MemoryTracker::kAlignment * MemoryTracker::kAlignment;
}

// Size-keyed list of reusable blocks (rounded size -> block).
std::mutex& cacheMutex() {
  static std::mutex m;
  return m;
}
std::multimap<std::size_t, void*>& cacheMap() {
  // Never destroyed: worker threads may release buffers during process exit.
  static auto* map = new std::multimap<std::size_t, void*>();
  return *map;
}

}  // namespace

MemoryTracker& MemoryTracker::instance() {
  static MemoryTracker tracker;
  return tracker;
}

void* MemoryTracker::takeCached(std::size_t rounded) noexcept {
  std::lock_guard<std::mutex> lock(cacheMutex());
  auto& map = cacheMap();
  auto it = map.find(rounded);
  if (it == map.end()) return nullptr;
  void* ptr = it->second;
  map.erase(it);
  cached_.fetch_sub(rounded, std::memory_order_relaxed);
  reused_.fetch_add(1, std::memory_order_relaxed);
  return ptr;
}

bool MemoryTracker::cache(void* ptr, std::size_t rounded) noexcept {
  if (rounded < kReuseMinBytes) return false;
  const std::uint64_t limit = reuseLimit_.load(std::memory_order_relaxed);
  const std::uint64_t budget = budget_.load(std::memory_order_relaxed);
  // Never keep more than a quarter of the budget idle.
  const std::uint64_t cap = budget != 0 ? std::min<std::uint64_t>(limit, budget / 4) : limit;
  std::lock_guard<std::mutex> lock(cacheMutex());
  if (cached_.load(std::memory_order_relaxed) + rounded > cap) return false;
  try {
    cacheMap().emplace(rounded, ptr);
  } catch (...) {
    return false;
  }
  cached_.fetch_add(rounded, std::memory_order_relaxed);
  return true;
}

bool MemoryTracker::evictFor(std::uint64_t needed) noexcept {
  // Frees kept blocks (largest first) until `needed` more bytes fit the budget.
  const std::uint64_t budget = budget_.load(std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(cacheMutex());
  auto& map = cacheMap();
  auto fits = [&] {
    const std::uint64_t held = current_.load(std::memory_order_relaxed) + cached_.load(std::memory_order_relaxed);
    return budget == 0 || held + needed <= budget;
  };
  while (!map.empty() && !fits()) {
    auto last = std::prev(map.end());
    std::free(last->second);
    cached_.fetch_sub(last->first, std::memory_order_relaxed);
    map.erase(last);
  }
  return fits();
}

void* MemoryTracker::allocate(std::size_t bytes, pp_status* status) {
  if (bytes == 0) {
    if (status) *status = PP_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }
  const std::size_t rounded = roundUp(bytes);

  // A block of the same size kept from earlier work: no new memory is held.
  void* ptr = takeCached(rounded);
  const bool reused = ptr != nullptr;

  // Reserve against the budget first using CAS so concurrent jobs cannot
  // jointly overshoot it. Idle kept blocks count too, and are freed first.
  const std::uint64_t budget = budget_.load(std::memory_order_relaxed);
  if (!reused && budget != 0) evictFor(bytes);
  std::uint64_t current = current_.load(std::memory_order_relaxed);
  std::uint64_t next = 0;
  do {
    next = current + bytes;
    const std::uint64_t idle = reused ? 0 : cached_.load(std::memory_order_relaxed);
    if (budget != 0 && next + idle > budget) {
      if (reused) std::free(ptr);
      failed_.fetch_add(1, std::memory_order_relaxed);
      if (status) *status = PP_ERROR_BUDGET_EXCEEDED;
      return nullptr;
    }
  } while (!current_.compare_exchange_weak(current, next, std::memory_order_relaxed));

  if (!reused) {
    // aligned_alloc requires the size to be a multiple of the alignment.
    ptr = std::aligned_alloc(kAlignment, rounded);
    if (ptr == nullptr) {
      current_.fetch_sub(bytes, std::memory_order_relaxed);
      failed_.fetch_add(1, std::memory_order_relaxed);
      if (status) *status = PP_ERROR_OUT_OF_MEMORY;
      return nullptr;
    }
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
  current_.fetch_sub(bytes, std::memory_order_relaxed);
  live_.fetch_sub(1, std::memory_order_relaxed);
  if (!cache(ptr, roundUp(bytes))) std::free(ptr);
}

void MemoryTracker::trim() noexcept {
  std::lock_guard<std::mutex> lock(cacheMutex());
  for (auto& entry : cacheMap()) std::free(entry.second);
  cacheMap().clear();
  cached_.store(0, std::memory_order_relaxed);
}

void MemoryTracker::setReuseLimit(std::uint64_t bytes) noexcept {
  reuseLimit_.store(bytes, std::memory_order_relaxed);
  if (bytes == 0) trim();
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
  s.cached_bytes = cached_.load(std::memory_order_relaxed);
  s.reused_allocations = reused_.load(std::memory_order_relaxed);
  return s;
}

}  // namespace photoplus
