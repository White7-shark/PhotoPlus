#include "photoplus/profiler.hpp"

#include <algorithm>
#include <cstdio>

namespace photoplus {

Profiler& Profiler::instance() {
  static Profiler profiler;
  return profiler;
}

void Profiler::record(const char* name, double ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  ProfileEntry& e = entries_[name];
  e.calls += 1;
  e.totalMs += ms;
  e.lastMs = ms;
  e.maxMs = std::max(e.maxMs, ms);
}

void Profiler::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
}

std::map<std::string, ProfileEntry> Profiler::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_;
}

std::string Profiler::toJson() const {
  const auto entries = snapshot();
  std::string out = "{";
  bool first = true;
  char buf[256];
  for (const auto& [name, e] : entries) {
    // Operation names are compile-time identifiers ([a-z_.]), so no escaping
    // is required.
    std::snprintf(buf, sizeof(buf),
                  "%s\"%s\":{\"calls\":%llu,\"totalMs\":%.3f,\"avgMs\":%.3f,\"maxMs\":%.3f,"
                  "\"lastMs\":%.3f}",
                  first ? "" : ",", name.c_str(), static_cast<unsigned long long>(e.calls),
                  e.totalMs, e.calls ? e.totalMs / static_cast<double>(e.calls) : 0.0, e.maxMs,
                  e.lastMs);
    out += buf;
    first = false;
  }
  out += "}";
  return out;
}

}  // namespace photoplus
