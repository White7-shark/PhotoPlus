#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "photoplus/pp_engine.h"

namespace photoplus {

// Process-wide accounting for every pixel buffer the engine allocates.
//
// Phones kill apps that grow without bound, so the engine enforces an
// optional budget (set by the host from the device's RAM class) and fails an
// allocation cleanly instead of letting the OS terminate the process.
class MemoryTracker {
 public:
  static MemoryTracker& instance();

  // Returns nullptr when the allocation would exceed the budget or the
  // system is out of memory. `status` receives the reason.
  void* allocate(std::size_t bytes, pp_status* status);
  void release(void* ptr, std::size_t bytes) noexcept;

  void setBudget(std::uint64_t bytes) noexcept { budget_.store(bytes, std::memory_order_relaxed); }
  std::uint64_t budget() const noexcept { return budget_.load(std::memory_order_relaxed); }
  void resetPeak() noexcept;

  pp_memory_stats stats() const noexcept;
  // Frees every buffer kept for reuse (memory pressure, or after a job).
  void trim() noexcept;
  // Most memory kept for reuse; 0 disables reuse.
  void setReuseLimit(std::uint64_t bytes) noexcept;

  static constexpr std::size_t kAlignment = 64;  // cache line; friendly to NEON loads
  // Buffers smaller than this are not worth keeping.
  static constexpr std::size_t kReuseMinBytes = 64 * 1024;

 private:
  MemoryTracker() = default;

  // Buffers released by finished work are kept for the next allocation of
  // the same size (pipelines allocate the same plane sizes over and over).
  // They count against the budget and are freed first when it is tight.
  void* takeCached(std::size_t rounded) noexcept;
  bool cache(void* ptr, std::size_t rounded) noexcept;
  bool evictFor(std::uint64_t needed) noexcept;
  std::atomic<std::uint64_t> cached_{0};
  std::atomic<std::uint64_t> reuseLimit_{96ull * 1024 * 1024};
  std::atomic<std::uint64_t> reused_{0};

  std::atomic<std::uint64_t> current_{0};
  std::atomic<std::uint64_t> peak_{0};
  std::atomic<std::uint64_t> budget_{0};
  std::atomic<std::uint64_t> live_{0};
  std::atomic<std::uint64_t> total_{0};
  std::atomic<std::uint64_t> failed_{0};
};

}  // namespace photoplus
