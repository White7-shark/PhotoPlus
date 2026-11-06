// stb_image / stb_image_write implementation unit (public domain, v2.30 /
// v1.16, vendored in third_party/stb). Compiled without the engine's warning
// set (third-party code), with:
//  - only the formats we need (JPEG, PNG, BMP, GIF): less attack surface;
//  - a dimension cap matching the engine's (decompression bombs fail early);
//  - every allocation tracked against the engine's memory budget.
#include <cstddef>
#include <cstring>

#include "photoplus/image.hpp"
#include "photoplus/memory.hpp"

namespace {
constexpr std::size_t kHeader = 16;

void* trackedMalloc(std::size_t n) {
  pp_status status = PP_OK;
  void* p = photoplus::MemoryTracker::instance().allocate(n + kHeader, &status);
  if (p == nullptr) return nullptr;
  *static_cast<std::size_t*>(p) = n;
  return static_cast<char*>(p) + kHeader;
}

void trackedFree(void* p) {
  if (p == nullptr) return;
  char* block = static_cast<char*>(p) - kHeader;
  const std::size_t n = *reinterpret_cast<std::size_t*>(block);
  photoplus::MemoryTracker::instance().release(block, n + kHeader);
}

void* trackedRealloc(void* p, std::size_t n) {
  if (p == nullptr) return trackedMalloc(n);
  const std::size_t old = *reinterpret_cast<std::size_t*>(static_cast<char*>(p) - kHeader);
  void* q = trackedMalloc(n);
  if (q == nullptr) return nullptr;
  std::memcpy(q, p, old < n ? old : n);
  trackedFree(p);
  return q;
}
}  // namespace

#define STBI_MALLOC(sz) trackedMalloc(sz)
#define STBI_REALLOC(p, newsz) trackedRealloc(p, newsz)
#define STBI_FREE(p) trackedFree(p)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBIW_MALLOC(sz) trackedMalloc(sz)
#define STBIW_REALLOC(p, newsz) trackedRealloc(p, newsz)
#define STBIW_FREE(p) trackedFree(p)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static_assert(STBI_MAX_DIMENSIONS == photoplus::kMaxDimension, "codec cap must match the engine's");
