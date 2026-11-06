#pragma once

#include <algorithm>
#include <atomic>

namespace photoplus {

// Cooperative cancellation flag and progress for one job. Operations poll it
// between rows so a cancel request from the UI thread stops work within a
// few milliseconds, and report how far they are (read by the host to show
// progress; never decreases).
class CancellationToken {
 public:
  void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
  bool isCancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

  // 0..1 overall.
  float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }

  // The share of the job the current step covers (e.g. operation i of n).
  void beginStep(float from, float to) const noexcept {
    stepFrom_.store(std::clamp(from, 0.0f, 1.0f), std::memory_order_relaxed);
    stepTo_.store(std::clamp(to, 0.0f, 1.0f), std::memory_order_relaxed);
    stepDone_.store(0.0f, std::memory_order_relaxed);
    raise(from);
  }

  // Work finished within the current step (fractions of the step; capped).
  void advance(float fraction) const noexcept {
    float done = stepDone_.load(std::memory_order_relaxed);
    float next = 0.0f;
    do {
      next = std::min(1.0f, done + fraction);
    } while (!stepDone_.compare_exchange_weak(done, next, std::memory_order_relaxed));
    const float from = stepFrom_.load(std::memory_order_relaxed);
    const float to = stepTo_.load(std::memory_order_relaxed);
    // A step may run several passes; keep a little room so the bar never
    // sits at the end of a step that still has work to do.
    raise(from + (to - from) * 0.95f * next);
  }

  void finish() const noexcept { raise(1.0f); }

 private:
  void raise(float value) const noexcept {
    float cur = progress_.load(std::memory_order_relaxed);
    while (value > cur && !progress_.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
    }
  }

  std::atomic<bool> cancelled_{false};
  mutable std::atomic<float> progress_{0.0f};
  mutable std::atomic<float> stepFrom_{0.0f};
  mutable std::atomic<float> stepTo_{1.0f};
  mutable std::atomic<float> stepDone_{0.0f};
};

inline bool isCancelled(const CancellationToken* token) noexcept {
  return token != nullptr && token->isCancelled();
}

}  // namespace photoplus
