#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "photoplus/cancellation.hpp"

namespace photoplus {

// Number of threads used for row-parallel image work. Capped so the engine
// never saturates every core of a phone (the UI and camera pipeline need
// headroom) and so peak scratch memory stays predictable.
inline int workerCount() {
  const unsigned hw = std::thread::hardware_concurrency();
  return static_cast<int>(std::clamp(hw == 0 ? 2u : hw, 1u, 4u));
}

// Runs the persistent pool's tasks (see thread_pool.hpp).
void runParallel(int32_t tasks, const std::function<void(int32_t)>& task);

// Runs fn(begin, end) over [0, count) in contiguous chunks on the worker
// pool. Chunks are smaller than one per thread so uneven rows balance out and
// progress moves smoothly; each finished chunk is reported to the token.
// Returns false if the token was cancelled (work may be partial).
template <typename Fn>
bool parallelFor(int32_t count, const CancellationToken* token, Fn&& fn) {
  if (count <= 0) return !isCancelled(token);
  const int32_t chunks = std::clamp<int32_t>(count / 16, 1, workerCount() * 4);
  if (chunks <= 1) {
    fn(0, count);
    if (token != nullptr) token->advance(1.0f);
    return !isCancelled(token);
  }
  const int32_t size = (count + chunks - 1) / chunks;
  runParallel(chunks, [&](int32_t c) {
    const int32_t begin = c * size;
    const int32_t end = std::min(count, begin + size);
    if (begin >= end || isCancelled(token)) return;
    fn(begin, end);
    if (token != nullptr) token->advance(static_cast<float>(end - begin) / static_cast<float>(count));
  });
  return !isCancelled(token);
}

}  // namespace photoplus
