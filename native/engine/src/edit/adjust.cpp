#include <algorithm>
#include <array>
#include <cmath>

#include "../portrait/common.hpp"
#include "photoplus/edit.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::edit {
namespace {

using portrait::detail::hashNoise;
using portrait::detail::isColour;
using portrait::detail::offsets;
using portrait::detail::Rgb;
using portrait::detail::smoothstep;

Error ok() { return Error{PP_OK, {}}; }

inline uint8_t byteOf(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f))); }

}  // namespace

// ---- Straighten -------------------------------------------------------------

Result<ImageBuffer> straighten(const ImageBuffer& src, float degrees, const CancellationToken* token) {
  ScopedTimer timer("edit.straighten");
  if (!isColour(src)) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Straighten needs a colour photo"));
  if (!std::isfinite(degrees)) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Angle must be finite"));
  const float deg = std::clamp(degrees, -45.0f, 45.0f);
  const float th = deg * 3.14159265f / 180.0f;
  const float c = std::fabs(std::cos(th)), s = std::fabs(std::sin(th));
  const auto W = static_cast<float>(src.width()), H = static_cast<float>(src.height());
  // Largest same-aspect rectangle inside the rotated photo.
  const float k = std::min(1.0f / (c + (H / W) * s), 1.0f / (c + (W / H) * s));
  const int32_t ow = std::max(1, static_cast<int32_t>(std::floor(W * k)));
  const int32_t oh = std::max(1, static_cast<int32_t>(std::floor(H * k)));
  auto out = ImageBuffer::allocate(ow, oh, src.format());
  if (!out) return failure(out.error());
  const float cosT = std::cos(th), sinT = std::sin(th);
  const float scx = 0.5f * (W - 1.0f), scy = 0.5f * (H - 1.0f);
  const float ocx = 0.5f * static_cast<float>(ow - 1), ocy = 0.5f * static_cast<float>(oh - 1);
  const int32_t sw = src.width(), sh = src.height();
  const bool done = parallelFor(oh, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* dst = out->row(y);
      const float dy = static_cast<float>(y) - ocy;
      for (int32_t x = 0; x < ow; ++x) {
        const float dx = static_cast<float>(x) - ocx;
        // Inverse rotation (the output is the photo turned clockwise by deg).
        const float fx = std::clamp(scx + dx * cosT + dy * sinT, 0.0f, W - 1.0f);
        const float fy = std::clamp(scy - dx * sinT + dy * cosT, 0.0f, H - 1.0f);
        const int32_t x0 = std::min(static_cast<int32_t>(fx), sw - 2 < 0 ? 0 : sw - 2);
        const int32_t y0 = std::min(static_cast<int32_t>(fy), sh - 2 < 0 ? 0 : sh - 2);
        const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
        const uint8_t* r0 = src.row(y0) + static_cast<size_t>(x0) * 4;
        const uint8_t* r1 = src.row(std::min(y0 + 1, sh - 1)) + static_cast<size_t>(x0) * 4;
        const int nx = x0 + 1 < sw ? 4 : 0;
        for (int ch = 0; ch < 4; ++ch) {
          const float top = r0[ch] + (static_cast<float>(r0[ch + nx]) - r0[ch]) * tx;
          const float bot = r1[ch] + (static_cast<float>(r1[ch + nx]) - r1[ch]) * tx;
          dst[static_cast<size_t>(x) * 4 + ch] = byteOf(top + (bot - top) * ty);
        }
      }
    }
  });
  if (!done) return failure(cancelledError());
  return out;
}

// ---- Warmth -----------------------------------------------------------------

Error warmth(ImageBuffer& image, float temperature, float tint, const CancellationToken* token) {
  ScopedTimer timer("edit.warmth");
  if (!isColour(image)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Warmth needs a colour photo");
  if (!std::isfinite(temperature) || !std::isfinite(tint)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Warmth must be finite");
  const float t = std::clamp(temperature, -1.0f, 1.0f), g = std::clamp(tint, -1.0f, 1.0f);
  // Gains on the light (blacks stay black), normalised so brightness holds.
  float gr = 1.0f + 0.16f * t + 0.04f * g, gg = 1.0f - 0.08f * g, gb = 1.0f - 0.16f * t + 0.04f * g;
  const float norm = 1.0f / (0.299f * gr + 0.587f * gg + 0.114f * gb);
  gr *= norm;
  gg *= norm;
  gb *= norm;
  std::array<std::array<uint8_t, 256>, 3> lut{};
  for (int i = 0; i < 256; ++i) {
    const auto v = static_cast<float>(i);
    lut[0][static_cast<size_t>(i)] = byteOf(v * gr);
    lut[1][static_cast<size_t>(i)] = byteOf(v * gg);
    lut[2][static_cast<size_t>(i)] = byteOf(v * gb);
  }
  const Rgb o = offsets(image);
  const bool done = parallelFor(image.height(), token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* p = image.row(y);
      for (int32_t x = 0; x < image.width(); ++x, p += 4) {
        p[o.r] = lut[0][p[o.r]];
        p[o.g] = lut[1][p[o.g]];
        p[o.b] = lut[2][p[o.b]];
      }
    }
  });
  return done ? ok() : cancelledError();
}

// ---- Looks ------------------------------------------------------------------

namespace {
struct Look {
  float exposure = 0.0f;     // stops
  float contrast = 0.0f;     // S-curve, -0.3..0.5
  float fade = 0.0f;         // lifted blacks / softened whites
  float saturation = 0.0f;   // -1..1
  float vibrance = 0.0f;     // boosts muted colours more
  float temp = 0.0f, tint = 0.0f;
  std::array<float, 3> shadows{0, 0, 0};     // colour added to the shadows
  std::array<float, 3> highlights{0, 0, 0};  // colour added to the highlights
  bool mono = false;
  std::array<float, 3> mix{0.299f, 0.587f, 0.114f};
  float vignette = 0.0f;
  float grain = 0.0f;
};

// Order matches pp_filter_id (1-based ids).
const std::array<Look, kFilterCount>& looks() {
  static const std::array<Look, kFilterCount> table = [] {
    std::array<Look, kFilterCount> t{};
    // 1 Natural: a touch of depth and colour.
    t[0].contrast = 0.12f;
    t[0].vibrance = 0.2f;
    // 2 Vivid.
    t[1].contrast = 0.22f;
    t[1].saturation = 0.22f;
    t[1].vibrance = 0.25f;
    // 3 Warm.
    t[2].temp = 0.45f;
    t[2].contrast = 0.08f;
    t[2].highlights = {0.02f, 0.01f, -0.02f};
    t[2].saturation = 0.05f;
    // 4 Cool.
    t[3].temp = -0.4f;
    t[3].contrast = 0.1f;
    t[3].shadows = {-0.01f, 0.0f, 0.03f};
    // 5 Mono.
    t[4].mono = true;
    t[4].contrast = 0.15f;
    // 6 Fade.
    t[5].fade = 0.1f;
    t[5].contrast = -0.1f;
    t[5].saturation = -0.2f;
    t[5].shadows = {0.02f, 0.01f, 0.03f};
    // 7 Soft.
    t[6].exposure = 0.15f;
    t[6].contrast = -0.15f;
    t[6].fade = 0.05f;
    t[6].saturation = -0.1f;
    t[6].highlights = {0.02f, 0.01f, 0.02f};
    // 8 Noir.
    t[7].mono = true;
    t[7].mix = {0.45f, 0.45f, 0.1f};
    t[7].contrast = 0.45f;
    t[7].vignette = 0.45f;
    t[7].grain = 0.015f;
    // 9 Cinema: teal shadows, warm highlights.
    t[8].contrast = 0.2f;
    t[8].shadows = {-0.03f, 0.01f, 0.05f};
    t[8].highlights = {0.05f, 0.02f, -0.04f};
    t[8].saturation = -0.05f;
    t[8].vignette = 0.2f;
    // 10 Golden Hour.
    t[9].temp = 0.7f;
    t[9].exposure = 0.1f;
    t[9].highlights = {0.06f, 0.03f, -0.02f};
    t[9].vibrance = 0.2f;
    t[9].contrast = 0.1f;
    t[9].vignette = 0.15f;
    // 11 Film.
    t[10].contrast = 0.15f;
    t[10].fade = 0.06f;
    t[10].saturation = -0.08f;
    t[10].shadows = {0.0f, 0.015f, 0.02f};
    t[10].highlights = {0.03f, 0.02f, 0.0f};
    t[10].grain = 0.025f;
    t[10].vignette = 0.1f;
    // 12 Lush.
    t[11].vibrance = 0.4f;
    t[11].saturation = 0.1f;
    t[11].contrast = 0.18f;
    t[11].shadows = {-0.01f, 0.015f, 0.0f};
    return t;
  }();
  return table;
}
}  // namespace

Error filter(ImageBuffer& image, int32_t id, float strength, const CancellationToken* token) {
  ScopedTimer timer("edit.filter");
  if (!isColour(image)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Looks need a colour photo");
  if (id < 1 || id > kFilterCount) return makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown look");
  if (!std::isfinite(strength)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Strength must be finite");
  const float k = std::clamp(strength, 0.0f, 1.0f);
  if (k <= 0.0f) return ok();
  const Look& L = looks()[static_cast<size_t>(id - 1)];
  // Tone curve: exposure, then an S-curve around mid grey, then fade.
  std::array<float, 256> curve{};
  const float gain = std::exp2(L.exposure);
  for (int i = 0; i < 256; ++i) {
    float v = std::min(1.0f, static_cast<float>(i) / 255.0f * gain);
    const float sv = v * v * (3.0f - 2.0f * v);  // smoothstep: an S
    v = v + L.contrast * (sv - v) * 2.0f;
    v = L.fade + v * (1.0f - 1.6f * L.fade);
    curve[static_cast<size_t>(i)] = std::clamp(v, 0.0f, 1.0f);
  }
  const float gr = 1.0f + 0.16f * L.temp + 0.04f * L.tint, gg = 1.0f - 0.08f * L.tint, gb = 1.0f - 0.16f * L.temp + 0.04f * L.tint;
  const Rgb o = offsets(image);
  const int32_t W = image.width(), H = image.height();
  const float cx = 0.5f * static_cast<float>(W - 1), cy = 0.5f * static_cast<float>(H - 1);
  const float invR2 = 1.0f / (cx * cx + cy * cy);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* p = image.row(y);
      const float dy2 = (static_cast<float>(y) - cy) * (static_cast<float>(y) - cy);
      for (int32_t x = 0; x < W; ++x, p += 4) {
        const float r0 = p[o.r] / 255.0f, g0 = p[o.g] / 255.0f, b0 = p[o.b] / 255.0f;
        auto at = [&](float v) {
          const float f = std::clamp(v, 0.0f, 1.0f) * 255.0f;
          const auto i = std::min<size_t>(static_cast<size_t>(f), 254);
          return curve[i] + (curve[i + 1] - curve[i]) * (f - static_cast<float>(i));
        };
        float r = at(r0 * gr), g = at(g0 * gg), b = at(b0 * gb);
        const float l = 0.299f * r + 0.587f * g + 0.114f * b;
        // Split toning: colour in the shadows and highlights.
        const float sh = (1.0f - l) * (1.0f - l), hi = l * l;
        r += L.shadows[0] * sh + L.highlights[0] * hi;
        g += L.shadows[1] * sh + L.highlights[1] * hi;
        b += L.shadows[2] * sh + L.highlights[2] * hi;
        if (L.mono) {
          const float m = L.mix[0] * r + L.mix[1] * g + L.mix[2] * b;
          r = g = b = m;
        } else {
          const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
          const float sat = std::max({r, g, b}) - std::min({r, g, b});
          const float s = 1.0f + L.saturation + L.vibrance * (1.0f - std::min(1.0f, sat * 2.5f));
          r = lum + (r - lum) * s;
          g = lum + (g - lum) * s;
          b = lum + (b - lum) * s;
        }
        if (L.vignette > 0.0f) {
          const float dx = static_cast<float>(x) - cx;
          const float d2 = (dx * dx + dy2) * invR2;
          const float v = 1.0f - L.vignette * smoothstep(0.25f, 1.0f, d2);
          r *= v;
          g *= v;
          b *= v;
        }
        if (L.grain > 0.0f) {
          const float n = L.grain * hashNoise(x, y, 0xF11Au);
          r += n;
          g += n;
          b += n;
        }
        p[o.r] = byteOf((r0 + (r - r0) * k) * 255.0f);
        p[o.g] = byteOf((g0 + (g - g0) * k) * 255.0f);
        p[o.b] = byteOf((b0 + (b - b0) * k) * 255.0f);
      }
    }
  });
  return done ? ok() : cancelledError();
}

// ---- Focus ------------------------------------------------------------------

Error focus(ImageBuffer& image, const FocusParams& fp, const CancellationToken* token) {
  ScopedTimer timer("edit.focus");
  if (!isColour(image)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Focus needs a colour photo");
  if (fp.kind != 1 && fp.kind != 2) return makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown focus kind");
  for (float v : {fp.cx, fp.cy, fp.size, fp.angleDeg, fp.strength, fp.feather}) {
    if (!std::isfinite(v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Focus parameters must be finite");
  }
  const float strength = std::clamp(fp.strength, 0.0f, 1.0f);
  if (strength <= 0.0f) return ok();
  const int32_t W = image.width(), H = image.height();
  const float shortSide = static_cast<float>(std::min(W, H));
  const float sigma = std::max(0.6f, 0.03f * shortSide * strength);
  // Blur on a reduced grid (cost independent of the blur size), two levels.
  const int32_t d = std::max<int32_t>(1, static_cast<int32_t>(std::lround(sigma / 4.0f)));
  const int32_t gw = std::max(2, (W + d - 1) / d), gh = std::max(2, (H + d - 1) / d);
  const Rgb o = offsets(image);
  std::array<Plane, 3> grid, soft, softer;
  for (int ch = 0; ch < 3; ++ch) {
    auto p = Plane::allocate(gw, gh);
    if (!p) return p.error();
    grid[static_cast<size_t>(ch)] = std::move(p).value();
  }
  for (int32_t gy = 0; gy < gh; ++gy) {
    for (int32_t gx = 0; gx < gw; ++gx) {
      std::array<float, 3> acc{};
      int32_t n = 0;
      for (int32_t y = gy * d; y < std::min(H, (gy + 1) * d); ++y) {
        const uint8_t* p = image.row(y) + static_cast<size_t>(gx * d) * 4;
        for (int32_t x = gx * d; x < std::min(W, (gx + 1) * d); ++x, p += 4) {
          acc[0] += p[o.r];
          acc[1] += p[o.g];
          acc[2] += p[o.b];
          ++n;
        }
      }
      for (int ch = 0; ch < 3; ++ch) grid[static_cast<size_t>(ch)].row(gy)[gx] = n > 0 ? acc[static_cast<size_t>(ch)] / static_cast<float>(n) : 0.0f;
    }
  }
  if (isCancelled(token)) return cancelledError();
  const float gs = sigma / static_cast<float>(d);
  for (int ch = 0; ch < 3; ++ch) {
    auto a = Plane::allocate(gw, gh), b = Plane::allocate(gw, gh);
    if (!a || !b) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for focus");
    if (Error e = gaussianBlur(grid[static_cast<size_t>(ch)], *a, 0.5f * gs, token); e.status != PP_OK) return e;
    if (Error e = gaussianBlur(grid[static_cast<size_t>(ch)], *b, gs, token); e.status != PP_OK) return e;
    soft[static_cast<size_t>(ch)] = std::move(a).value();
    softer[static_cast<size_t>(ch)] = std::move(b).value();
  }
  const float cxp = std::clamp(fp.cx, 0.0f, 1.0f) * static_cast<float>(W);
  const float cyp = std::clamp(fp.cy, 0.0f, 1.0f) * static_cast<float>(H);
  const float size = std::max(0.02f, fp.size) * shortSide;
  const float feather = std::max(0.05f, std::clamp(fp.feather, 0.0f, 1.0f)) * shortSide * 0.6f;
  const float th = fp.angleDeg * 3.14159265f / 180.0f;
  const float nx = -std::sin(th), ny = std::cos(th);  // normal of the tilt band
  const float invD = 1.0f / static_cast<float>(d);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* p = image.row(y);
      const float fy = (static_cast<float>(y) + 0.5f) * invD - 0.5f;
      for (int32_t x = 0; x < W; ++x, p += 4) {
        const float dx = static_cast<float>(x) - cxp, dy = static_cast<float>(y) - cyp;
        const float dist = fp.kind == 1 ? std::sqrt(dx * dx + dy * dy) : std::fabs(dx * nx + dy * ny);
        const float m = smoothstep(size, size + feather, dist);
        if (m <= 0.0f) continue;
        const float fx = (static_cast<float>(x) + 0.5f) * invD - 0.5f;
        const int idx[3] = {o.r, o.g, o.b};
        for (size_t ch = 0; ch < 3; ++ch) {
          const float s1 = soft[ch].sample(fx, fy), s2 = softer[ch].sample(fx, fy);
          const float orig = p[idx[ch]];
          const float v = m < 0.5f ? orig + (s1 - orig) * (2.0f * m) : s1 + (s2 - s1) * (2.0f * m - 1.0f);
          p[idx[ch]] = byteOf(v);
        }
      }
    }
  });
  return done ? ok() : cancelledError();
}

}  // namespace photoplus::edit
