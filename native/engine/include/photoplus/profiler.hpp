#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace photoplus {

struct ProfileEntry {
  std::uint64_t calls = 0;
  double totalMs = 0.0;
  double maxMs = 0.0;
  double lastMs = 0.0;
};

// Thread-safe aggregate timings, keyed by operation name. Cheap enough to be
// always-on: one steady_clock read at scope entry and exit.
class Profiler {
 public:
  static Profiler& instance();

  void record(const char* name, double ms);
  void reset();
  std::map<std::string, ProfileEntry> snapshot() const;
  std::string toJson() const;

 private:
  Profiler() = default;
  mutable std::mutex mutex_;
  std::map<std::string, ProfileEntry> entries_;
};

class ScopedTimer {
 public:
  explicit ScopedTimer(const char* name) : name_(name), start_(std::chrono::steady_clock::now()) {}
  ~ScopedTimer() {
    const auto end = std::chrono::steady_clock::now();
    Profiler::instance().record(
        name_, std::chrono::duration<double, std::milli>(end - start_).count());
  }
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

 private:
  const char* name_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace photoplus
