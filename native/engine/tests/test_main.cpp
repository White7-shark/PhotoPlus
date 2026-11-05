#include <cstdio>
#include <cstring>

#include "test_harness.hpp"

namespace pptest {

namespace {
int g_failures = 0;
}

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

void reportFailure(const char* file, int line, const std::string& message) {
  ++g_failures;
  std::fprintf(stderr, "    FAIL %s:%d: %s\n", file, line, message.c_str());
}

}  // namespace pptest

// Optional argument: run only tests whose name contains it.
int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int failedTests = 0;
  size_t ran = 0;
  for (const auto& test : pptest::registry()) {
    if (filter != nullptr && std::strstr(test.name, filter) == nullptr) continue;
    ++ran;
    const int before = pptest::g_failures;
    std::printf("[ RUN  ] %s\n", test.name);
    test.fn();
    const bool passed = pptest::g_failures == before;
    std::printf("[ %s ] %s\n", passed ? " OK " : "FAIL", test.name);
    if (!passed) ++failedTests;
  }
  std::printf("\n%zu tests, %d failed\n", ran, failedTests);
  return failedTests == 0 ? 0 : 1;
}
