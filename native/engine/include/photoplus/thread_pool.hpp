#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace photoplus {

// Persistent worker threads for row-parallel image work. Threads are created
// once (not per operation), so short operations cost microseconds, not the
// price of starting threads.
//
// The calling thread always works on its own batch too, and a batch is never
// waited on by a thread that could be running it, so nested parallel loops
// (an operation that runs another one inside a task) cannot deadlock.
class ThreadPool {
 public:
  static ThreadPool& instance();
  ~ThreadPool();

  // Threads that can work at once, the caller included.
  int concurrency() const noexcept { return static_cast<int>(workers_.size()) + 1; }

  // Calls task(i) for every i in [0, count) and returns when all are done.
  void run(int32_t count, const std::function<void(int32_t)>& task);

 private:
  struct Batch {
    const std::function<void(int32_t)>* task = nullptr;
    int32_t count = 0;
    std::atomic<int32_t> next{0};
    std::atomic<int32_t> done{0};
  };

  explicit ThreadPool(int workers);
  void workerLoop();
  static void work(Batch& batch);

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable wake_;      // workers: new batches
  std::condition_variable finished_;  // callers: batch completion
  std::deque<std::shared_ptr<Batch>> queue_;
  bool stopping_ = false;
};

}  // namespace photoplus
