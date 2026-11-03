#pragma once

#include <expected>
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

// Fallible results use C++23 std::expected. `failure(error)` builds the
// unexpected branch; in-place operations simply return an Error (PP_OK = ok).
template <typename T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> failure(Error error) { return std::unexpected<Error>(std::move(error)); }

inline Error makeError(pp_status status, std::string message) {
  return Error{status, std::move(message)};
}

}  // namespace photoplus
