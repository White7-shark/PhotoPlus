#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Portable image decoding and encoding (JPEG, PNG; BMP and GIF read-only).
// Hosts with a platform decoder (Android's ImageDecoder handles HEIF and uses
// hardware) may keep using it; this codec gives the engine and its tools one
// that behaves the same everywhere, with safety limits:
//  - dimensions are checked from the header before any pixel is decoded;
//  - every allocation counts toward the engine budget, so a decompression
//    bomb fails with PP_ERROR_BUDGET_EXCEEDED instead of exhausting memory;
//  - JPEG EXIF orientation is applied, so pixels come out upright.
namespace photoplus::codec {

enum class Format : int32_t { Unknown = 0, Jpeg = 1, Png = 2, Bmp = 3, Gif = 4 };

struct DecodeOptions {
  int32_t maxEdge = 0;          // > 0: shrink (area filter) so the longer edge fits
  bool applyOrientation = true; // JPEG EXIF orientation
};

struct DecodeInfo {
  int32_t width = 0, height = 0;              // of the returned image
  int32_t sourceWidth = 0, sourceHeight = 0;  // as stored in the file
  int32_t orientation = 1;                    // EXIF 1..8 (1 = upright)
  Format format = Format::Unknown;
  bool hasAlpha = false;
};

Format sniff(const uint8_t* data, std::size_t size);
// EXIF orientation of a JPEG (1 when absent or unreadable).
int32_t exifOrientation(const uint8_t* data, std::size_t size);

// Decoded images are RGBA8 with straight alpha.
Result<ImageBuffer> decode(const uint8_t* data, std::size_t size, const DecodeOptions& options,
                           const CancellationToken* token, DecodeInfo* info);
Result<ImageBuffer> decodeFile(const char* path, const DecodeOptions& options, const CancellationToken* token,
                               DecodeInfo* info);

// quality 1..100 (JPEG only). PNG keeps alpha; JPEG drops it.
Result<std::vector<uint8_t>> encode(const ImageBuffer& image, Format format, int32_t quality);
// Written to a temporary file first, then renamed: never a half-written file.
Error encodeFile(const ImageBuffer& image, const char* path, Format format, int32_t quality);

}  // namespace photoplus::codec
