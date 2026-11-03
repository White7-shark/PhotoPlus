#pragma once

#include <atomic>

namespace photoplus {

// Cooperative cancellation flag. Operations poll it between rows so a cancel
// request from the UI thread stops work within a few milliseconds.
class CancellationToken {
 public:
  void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
  bool isCancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

 private:
  std::atomic<bool> cancelled_{false};
};

inline bool isCancelled(const CancellationToken* token) noexcept {
  return token != nullptr && token->isCancelled();
}

}  // namespace photoplus
