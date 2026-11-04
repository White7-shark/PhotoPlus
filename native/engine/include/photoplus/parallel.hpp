#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "photoplus/cancellation.hpp"

namespace photoplus {

// Number of worker threads used for row-parallel image work. Capped so the
// engine never saturates every core of a phone (the UI and camera pipeline
// need headroom) and so peak scratch memory stays predictable.
inline int workerCount() {
  const unsigned hw = std::thread::hardware_concurrency();
  return static_cast<int>(std::clamp(hw == 0 ? 2u : hw, 1u, 4u));
}

// Runs fn(begin, end) over [0, count) split into contiguous chunks, one per
// worker. Returns false if the token was cancelled (work may be partial).
template <typename Fn>
bool parallelFor(int32_t count, const CancellationToken* token, Fn&& fn) {
  if (count <= 0) return !isCancelled(token);
  const int workers = std::min<int>(workerCount(), std::max<int32_t>(1, count / 16));
  if (workers <= 1) {
    fn(0, count);
    return !isCancelled(token);
  }
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(workers - 1));
  const int32_t chunk = (count + workers - 1) / workers;
  for (int w = 1; w < workers; ++w) {
    const int32_t begin = w * chunk;
    const int32_t end = std::min(count, begin + chunk);
    if (begin >= end) break;
    threads.emplace_back([&fn, begin, end] { fn(begin, end); });
  }
  fn(0, std::min(count, chunk));
  for (auto& t : threads) t.join();
  return !isCancelled(token);
}

}  // namespace photoplus
