#include "photoplus/codec.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "../ops/common.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/memory.hpp"
#include "photoplus/operations.hpp"
#include "photoplus/profiler.hpp"

#define STBI_NO_STDIO
#define STBI_WRITE_NO_STDIO
#include "stb_image.h"
#include "stb_image_write.h"

namespace photoplus::codec {

using detail::Scratch;

Format sniff(const uint8_t* d, std::size_t n) {
  if (d == nullptr) return Format::Unknown;
  if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return Format::Jpeg;
  if (n >= 8 && std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0) return Format::Png;
  if (n >= 2 && d[0] == 'B' && d[1] == 'M') return Format::Bmp;
  if (n >= 6 && (std::memcmp(d, "GIF87a", 6) == 0 || std::memcmp(d, "GIF89a", 6) == 0)) return Format::Gif;
  return Format::Unknown;
}

int32_t exifOrientation(const uint8_t* d, std::size_t n) {
  if (sniff(d, n) != Format::Jpeg) return 1;
  std::size_t i = 2;
  while (i + 4 <= n) {
    if (d[i] != 0xFF) return 1;
    const uint8_t marker = d[i + 1];
    if (marker == 0xD9 || marker == 0xDA) return 1;  // end of image / start of scan
    const std::size_t len = (static_cast<std::size_t>(d[i + 2]) << 8) | d[i + 3];
    if (len < 2 || i + 2 + len > n) return 1;
    if (marker == 0xE1 && len >= 16 && std::memcmp(d + i + 4, "Exif\0\0", 6) == 0) {
      const uint8_t* t = d + i + 10;
      const std::size_t tn = len - 8;
      const bool le = t[0] == 'I' && t[1] == 'I';
      if (!le && !(t[0] == 'M' && t[1] == 'M')) return 1;
      auto u16 = [&](std::size_t o) -> uint32_t {
        if (o + 2 > tn) return 0;
        return le ? (t[o] | (t[o + 1] << 8)) : ((t[o] << 8) | t[o + 1]);
      };
      auto u32 = [&](std::size_t o) -> uint32_t {
        if (o + 4 > tn) return 0;
        return le ? (t[o] | (t[o + 1] << 8) | (t[o + 2] << 16) | (static_cast<uint32_t>(t[o + 3]) << 24))
                  : ((static_cast<uint32_t>(t[o]) << 24) | (t[o + 1] << 16) | (t[o + 2] << 8) | t[o + 3]);
      };
      if (u16(2) != 42) return 1;
      const std::size_t ifd = u32(4);
      const uint32_t count = u16(ifd);
      for (uint32_t e = 0; e < count && e < 512; ++e) {
        const std::size_t entry = ifd + 2 + 12 * static_cast<std::size_t>(e);
        if (entry + 12 > tn) return 1;
        if (u16(entry) == 0x0112) {
          const uint32_t v = u16(entry + 8);
          return v >= 1 && v <= 8 ? static_cast<int32_t>(v) : 1;
        }
      }
      return 1;
    }
    i += 2 + len;
  }
  return 1;
}

namespace {

Error codecError(pp_status status, const std::string& what) { return makeError(status, what); }

Result<ImageBuffer> orient(ImageBuffer img, int32_t orientation, const CancellationToken* token) {
  auto turn = [&](int quarter) -> Result<ImageBuffer> { return ops::rotate90(img, quarter, token); };
  auto flipH = [&](ImageBuffer& b) { return ops::flip(b, true, false, token); };
  auto flipV = [&](ImageBuffer& b) { return ops::flip(b, false, true, token); };
  switch (orientation) {
    case 2: {
      if (Error e = flipH(img); e.status != PP_OK) return failure(e);
      return img;
    }
    case 3:
      return turn(2);
    case 4: {
      if (Error e = flipV(img); e.status != PP_OK) return failure(e);
      return img;
    }
    case 5:
    case 7: {
      auto r = turn(1);
      if (!r) return r;
      if (Error e = orientation == 5 ? flipH(r.value()) : flipV(r.value()); e.status != PP_OK) return failure(e);
      return r;
    }
    case 6:
      return turn(1);
    case 8:
      return turn(3);
    default:
      return img;
  }
}

}  // namespace

Result<ImageBuffer> decode(const uint8_t* data, std::size_t size, const DecodeOptions& options,
                           const CancellationToken* token, DecodeInfo* info) {
  ScopedTimer timer("codec.decode");
  if (data == nullptr || size == 0) return failure(codecError(PP_ERROR_INVALID_ARGUMENT, "No image data"));
  if (size > static_cast<std::size_t>(INT32_MAX)) return failure(codecError(PP_ERROR_UNSUPPORTED_FORMAT, "Image file is too large"));
  if (options.maxEdge < 0) return failure(codecError(PP_ERROR_INVALID_ARGUMENT, "maxEdge must be 0 or more"));
  const Format format = sniff(data, size);
  if (format == Format::Unknown) return failure(codecError(PP_ERROR_UNSUPPORTED_FORMAT, "Unrecognised image format"));
  int w = 0, h = 0, comp = 0;
  const int len = static_cast<int>(size);
  if (!stbi_info_from_memory(data, len, &w, &h, &comp)) {
    return failure(codecError(PP_ERROR_UNSUPPORTED_FORMAT, std::string("Unreadable image: ") + stbi_failure_reason()));
  }
  if (w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension) {
    return failure(codecError(PP_ERROR_UNSUPPORTED_FORMAT, "Image dimensions are out of range"));
  }
  // Check the budget from the header, before decoding anything.
  const std::uint64_t need = static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4u * 2u;
  const std::uint64_t budget = MemoryTracker::instance().budget();
  if (budget != 0 && need > budget) {
    return failure(codecError(PP_ERROR_BUDGET_EXCEEDED, "Image is too large for the memory budget"));
  }
  if (isCancelled(token)) return failure(detail::cancelledErr());
  int dw = 0, dh = 0, dc = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, len, &dw, &dh, &dc, 4);
  if (pixels == nullptr) {
    const std::string why = stbi_failure_reason() ? stbi_failure_reason() : "unknown";
    const pp_status st = why.find("outofmem") != std::string::npos ? PP_ERROR_BUDGET_EXCEEDED : PP_ERROR_UNSUPPORTED_FORMAT;
    return failure(codecError(st, "Unable to decode image: " + why));
  }
  auto img = ImageBuffer::allocate(dw, dh, PixelFormat::RGBA8);
  if (!img) {
    stbi_image_free(pixels);
    return failure(img.error());
  }
  for (int32_t y = 0; y < dh; ++y) {
    std::memcpy(img->row(y), pixels + static_cast<std::size_t>(y) * dw * 4, static_cast<std::size_t>(dw) * 4);
  }
  stbi_image_free(pixels);
  const int32_t orientation = format == Format::Jpeg ? exifOrientation(data, size) : 1;
  Result<ImageBuffer> current = std::move(img);
  if (options.applyOrientation && orientation != 1) {
    current = orient(std::move(current).value(), orientation, token);
    if (!current) return current;
  }
  if (options.maxEdge > 0) {
    const int32_t cw = current->width(), ch = current->height();
    const int32_t longest = std::max(cw, ch);
    if (longest > options.maxEdge) {
      const double s = static_cast<double>(options.maxEdge) / longest;
      const int32_t tw = std::max(1, static_cast<int32_t>(std::lround(cw * s)));
      const int32_t th = std::max(1, static_cast<int32_t>(std::lround(ch * s)));
      current = ops::resize(current.value(), tw, th, ops::ResizeFilter::Area, token);
      if (!current) return current;
    }
  }
  if (info != nullptr) {
    info->width = current->width();
    info->height = current->height();
    info->sourceWidth = w;
    info->sourceHeight = h;
    info->orientation = orientation;
    info->format = format;
    info->hasAlpha = comp == 2 || comp == 4;
  }
  return current;
}

Result<ImageBuffer> decodeFile(const char* path, const DecodeOptions& options, const CancellationToken* token,
                               DecodeInfo* info) {
  if (path == nullptr) return failure(codecError(PP_ERROR_INVALID_ARGUMENT, "Path is null"));
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return failure(codecError(PP_ERROR_IO, "Unable to open the image file"));
  long size = -1;
  if (std::fseek(f, 0, SEEK_END) == 0) size = std::ftell(f);
  std::rewind(f);
  if (size <= 0) {
    std::fclose(f);
    return failure(codecError(size == 0 ? PP_ERROR_UNSUPPORTED_FORMAT : PP_ERROR_IO, "The image file is empty or unreadable"));
  }
  auto buf = Scratch<uint8_t>::allocate(static_cast<std::size_t>(size));
  if (!buf) {
    std::fclose(f);
    return failure(buf.error());
  }
  const std::size_t got = std::fread(buf->data(), 1, buf->size(), f);
  std::fclose(f);
  if (got != buf->size()) return failure(codecError(PP_ERROR_IO, "The image file could not be read completely"));
  return decode(buf->data(), buf->size(), options, token, info);
}

namespace {

void appendTo(void* context, void* data, int size) {
  auto* out = static_cast<std::vector<uint8_t>*>(context);
  const auto* bytes = static_cast<const uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
}

}  // namespace

Result<std::vector<uint8_t>> encode(const ImageBuffer& image, Format format, int32_t quality) {
  ScopedTimer timer("codec.encode");
  if (image.empty()) return failure(codecError(PP_ERROR_INVALID_ARGUMENT, "Empty image"));
  if (format != Format::Jpeg && format != Format::Png) {
    return failure(codecError(PP_ERROR_UNSUPPORTED_FORMAT, "Images can be saved as JPEG or PNG"));
  }
  const detail::Layout L = detail::layoutOf(image.format());
  const bool keepAlpha = format == Format::Png && L.alpha >= 0;
  const int comps = L.colors == 1 ? 1 : (keepAlpha ? 4 : 3);
  const int32_t w = image.width(), h = image.height();
  auto packed = Scratch<uint8_t>::allocate(static_cast<std::size_t>(w) * h * comps);
  if (!packed) return failure(packed.error());
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* s = image.row(y);
    uint8_t* d = packed->data() + static_cast<std::size_t>(y) * w * comps;
    for (int32_t x = 0; x < w; ++x) {
      const uint8_t* p = s + x * L.bpp;
      if (comps == 1) {
        d[x] = p[0];
      } else {
        d[x * comps] = p[L.r], d[x * comps + 1] = p[L.g], d[x * comps + 2] = p[L.b];
        if (comps == 4) d[x * comps + 3] = p[L.alpha];
      }
    }
  }
  std::vector<uint8_t> out;
  try {
    out.reserve(static_cast<std::size_t>(w) * h / 4);
  } catch (...) {
    return failure(codecError(PP_ERROR_OUT_OF_MEMORY, "Out of memory encoding"));
  }
  int okWrite = 0;
  if (format == Format::Png) {
    okWrite = stbi_write_png_to_func(appendTo, &out, w, h, comps, packed->data(), w * comps);
  } else {
    okWrite = stbi_write_jpg_to_func(appendTo, &out, w, h, comps, packed->data(), std::clamp(quality, 1, 100));
  }
  if (!okWrite || out.empty()) return failure(codecError(PP_ERROR_INTERNAL, "Encoding failed"));
  return out;
}

Error encodeFile(const ImageBuffer& image, const char* path, Format format, int32_t quality) {
  if (path == nullptr) return codecError(PP_ERROR_INVALID_ARGUMENT, "Path is null");
  auto bytes = encode(image, format, quality);
  if (!bytes) return bytes.error();
  const std::string target(path);
  const std::string tmp = target + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) return codecError(PP_ERROR_IO, "Unable to create the output file");
  const std::size_t wrote = std::fwrite(bytes->data(), 1, bytes->size(), f);
  const bool closed = std::fclose(f) == 0;
  if (wrote != bytes->size() || !closed || std::rename(tmp.c_str(), target.c_str()) != 0) {
    std::remove(tmp.c_str());
    return codecError(PP_ERROR_IO, "Unable to write the output file");
  }
  return detail::ok();
}

}  // namespace photoplus::codec
