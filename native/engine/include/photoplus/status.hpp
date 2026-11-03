#pragma once

#include <string>
#include <utility>

#include "photoplus/pp_engine.h"

namespace photoplus {

// Engine-wide error type. Maps 1:1 onto the C ABI status codes so errors can
// cross the boundary without translation tables.
struct Error {
  pp_status status = PP_ERROR_INTERNAL;
  std::string message;
};

// Minimal expected<T, Error>. The engine is compiled without exceptions in
// mind on the hot path, so failures travel as values.
template <typename T>
class Result {
 public:
  Result(T value) : ok_(true), value_(std::move(value)) {}  // NOLINT(implicit)
  Result(Error error) : ok_(false), error_(std::move(error)) {}  // NOLINT(implicit)

  bool ok() const { return ok_; }
  explicit operator bool() const { return ok_; }

  T& value() & { return value_; }
  T&& value() && { return std::move(value_); }
  const T& value() const& { return value_; }
  const Error& error() const { return error_; }

 private:
  bool ok_;
  T value_{};
  Error error_{};
};

inline Error makeError(pp_status status, std::string message) {
  return Error{status, std::move(message)};
}

}  // namespace photoplus
