#pragma once

// Tiny dependency-free test harness so the engine tests build anywhere
// (CI, offline machines) without fetching a framework.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace pptest {

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const std::string& message);

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace pptest

#define PP_CONCAT2(a, b) a##b
#define PP_CONCAT(a, b) PP_CONCAT2(a, b)

#define TEST(name)                                                              \
  static void name();                                                           \
  static ::pptest::Registrar PP_CONCAT(registrar_, name)(#name, name);          \
  static void name()

#define EXPECT_TRUE(cond)                                                       \
  do {                                                                          \
    if (!(cond)) ::pptest::reportFailure(__FILE__, __LINE__, "EXPECT_TRUE(" #cond ")"); \
  } while (0)

#define EXPECT_EQ(a, b)                                                         \
  do {                                                                          \
    const auto pp_a = (a);                                                      \
    const auto pp_b = (b);                                                      \
    if (!(pp_a == pp_b)) {                                                      \
      ::pptest::reportFailure(__FILE__, __LINE__,                               \
                              "EXPECT_EQ(" #a ", " #b ") -> " +                 \
                                  std::to_string(pp_a) + " vs " + std::to_string(pp_b)); \
    }                                                                           \
  } while (0)

#define EXPECT_NEAR(a, b, tol)                                                  \
  do {                                                                          \
    const double pp_a = static_cast<double>(a);                                 \
    const double pp_b = static_cast<double>(b);                                 \
    if (pp_a - pp_b > (tol) || pp_b - pp_a > (tol)) {                           \
      ::pptest::reportFailure(__FILE__, __LINE__,                               \
                              "EXPECT_NEAR(" #a ", " #b ") -> " +               \
                                  std::to_string(pp_a) + " vs " + std::to_string(pp_b)); \
    }                                                                           \
  } while (0)

#define ASSERT_OK(status)                                                       \
  do {                                                                          \
    const pp_status pp_s = (status);                                            \
    if (pp_s != PP_OK) {                                                        \
      ::pptest::reportFailure(__FILE__, __LINE__,                               \
                              std::string("ASSERT_OK(" #status ") -> ") +       \
                                  pp_status_name(pp_s) + ": " + pp_last_error_message()); \
      return;                                                                   \
    }                                                                           \
  } while (0)
