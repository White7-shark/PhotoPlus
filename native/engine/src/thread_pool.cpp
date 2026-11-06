#include "photoplus/thread_pool.hpp"

#include <algorithm>

#include "photoplus/parallel.hpp"

namespace photoplus {

ThreadPool& ThreadPool::instance() {
  // Never destroyed: work may still be finishing while the process exits.
  static ThreadPool* pool = new ThreadPool(workerCount() - 1);
  return *pool;
}

ThreadPool::ThreadPool(int workers) {
  workers_.reserve(static_cast<size_t>(std::max(0, workers)));
  for (int i = 0; i < workers; ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  for (auto& t : workers_) t.join();
}

void ThreadPool::work(Batch& batch) {
  for (;;) {
    const int32_t i = batch.next.fetch_add(1, std::memory_order_relaxed);
    if (i >= batch.count) return;
    try {
      (*batch.task)(i);
    } catch (...) {
      // Engine code reports errors by value; never let one kill a worker.
    }
    batch.done.fetch_add(1, std::memory_order_acq_rel);
  }
}

void ThreadPool::workerLoop() {
  for (;;) {
    std::shared_ptr<Batch> batch;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      for (;;) {
        // Batches with every task already handed out need no more workers.
        while (!queue_.empty() && queue_.front()->next.load(std::memory_order_relaxed) >= queue_.front()->count) {
          queue_.pop_front();
        }
        if (stopping_) return;
        if (!queue_.empty()) {
          batch = queue_.front();
          break;
        }
        wake_.wait(lock);
      }
    }
    work(*batch);
    if (batch->done.load(std::memory_order_acquire) >= batch->count) {
      std::lock_guard<std::mutex> lock(mutex_);
      finished_.notify_all();
    }
  }
}

void ThreadPool::run(int32_t count, const std::function<void(int32_t)>& task) {
  if (count <= 0) return;
  if (count == 1 || workers_.empty()) {
    for (int32_t i = 0; i < count; ++i) task(i);
    return;
  }
  auto batch = std::make_shared<Batch>();
  batch->task = &task;
  batch->count = count;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(batch);
  }
  wake_.notify_all();
  work(*batch);  // the caller works too
  {
    std::unique_lock<std::mutex> lock(mutex_);
    // Drop it from the queue if no worker did (all tasks are taken now).
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
      if (*it == batch) {
        queue_.erase(it);
        break;
      }
    }
    finished_.wait(lock, [&] { return batch->done.load(std::memory_order_acquire) >= batch->count; });
  }
}

}  // namespace photoplus

namespace photoplus {
void runParallel(int32_t tasks, const std::function<void(int32_t)>& task) { ThreadPool::instance().run(tasks, task); }
}  // namespace photoplus
