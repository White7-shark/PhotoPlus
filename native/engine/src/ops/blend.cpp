// Compositing with blend modes, and mask tools (feather, grow, refine).

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "common.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::ops {

using detail::Layout;
using detail::layoutOf;
using detail::toByte;

namespace {

struct Rgb {
  float r, g, b;
};

float lum(const Rgb& c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }

Rgb clipColor(Rgb c) {
  const float l = lum(c);
  const float n = std::min({c.r, c.g, c.b}), x = std::max({c.r, c.g, c.b});
  if (n < 0.0f) {
    c = {l + (c.r - l) * l / (l - n), l + (c.g - l) * l / (l - n), l + (c.b - l) * l / (l - n)};
  }
  if (x > 1.0f) {
    c = {l + (c.r - l) * (1 - l) / (x - l), l + (c.g - l) * (1 - l) / (x - l), l + (c.b - l) * (1 - l) / (x - l)};
  }
  return c;
}

Rgb setLum(const Rgb& c, float l) {
  const float d = l - lum(c);
  return clipColor({c.r + d, c.g + d, c.b + d});
}

float separable(BlendMode mode, float cb, float cs) {
  switch (mode) {
    case BlendMode::Multiply:
      return cb * cs;
    case BlendMode::Screen:
      return cb + cs - cb * cs;
    case BlendMode::Overlay:
      return cb <= 0.5f ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs);
    case BlendMode::HardLight:
      return cs <= 0.5f ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs);
    case BlendMode::SoftLight: {
      if (cs <= 0.5f) return cb - (1 - 2 * cs) * cb * (1 - cb);
      const float d = cb <= 0.25f ? ((16 * cb - 12) * cb + 4) * cb : std::sqrt(cb);
      return cb + (2 * cs - 1) * (d - cb);
    }
    case BlendMode::Darken:
      return std::min(cb, cs);
    case BlendMode::Lighten:
      return std::max(cb, cs);
    case BlendMode::Difference:
      return std::fabs(cb - cs);
    case BlendMode::Add:
      return std::min(1.0f, cb + cs);
    default:
      return cs;
  }
}

Rgb mix(BlendMode mode, const Rgb& b, const Rgb& s) {
  if (mode == BlendMode::Color) return setLum(s, lum(b));
  if (mode == BlendMode::Luminosity) return setLum(b, lum(s));
  return {separable(mode, b.r, s.r), separable(mode, b.g, s.g), separable(mode, b.b, s.b)};
}

}  // namespace

Error blend(ImageBuffer& dst, const ImageBuffer& src, const ImageBuffer* mask, int32_t x, int32_t y, BlendMode mode,
            float opacity, const CancellationToken* token) {
  ScopedTimer timer("op.blend");
  if (dst.empty() || src.empty()) return detail::invalid("Empty image");
  if (!std::isfinite(opacity)) return detail::invalid("Opacity must be finite");
  const int32_t m = static_cast<int32_t>(mode);
  if (m < 0 || m >= kBlendModeCount) return detail::invalid("Unknown blend mode");
  const Layout LD = layoutOf(dst.format()), LS = layoutOf(src.format());
  if (LD.colors != 3 || LS.colors != 3) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Blending needs colour images");
  if (mask != nullptr && (mask->format() != PixelFormat::Gray8 || mask->width() != src.width() || mask->height() != src.height())) {
    return detail::invalid("The mask must be a gray image the size of the layer");
  }
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  const int32_t x0 = std::max(0, x), y0 = std::max(0, y);
  const int32_t x1 = std::min(dst.width(), x + src.width()), y1 = std::min(dst.height(), y + src.height());
  if (x0 >= x1 || y0 >= y1 || opacity == 0.0f) return detail::ok();
  const bool ok = parallelFor(y1 - y0, token, [&](int32_t r0, int32_t r1) {
    for (int32_t r = r0; r < r1; ++r) {
      const int32_t dy = y0 + r, sy = dy - y;
      uint8_t* d = dst.row(dy);
      const uint8_t* s = src.row(sy);
      const uint8_t* mk = mask ? mask->row(sy) : nullptr;
      for (int32_t dx = x0; dx < x1; ++dx) {
        const int32_t sx = dx - x;
        const uint8_t* sp = s + sx * LS.bpp;
        uint8_t* dp = d + dx * LD.bpp;
        float as = (LS.alpha >= 0 ? sp[LS.alpha] / 255.0f : 1.0f) * opacity;
        if (mk) as *= mk[sx] / 255.0f;
        if (as <= 0.0f) continue;
        const float ab = LD.alpha >= 0 ? dp[LD.alpha] / 255.0f : 1.0f;
        const Rgb cs{sp[LS.r] / 255.0f, sp[LS.g] / 255.0f, sp[LS.b] / 255.0f};
        const Rgb cb{dp[LD.r] / 255.0f, dp[LD.g] / 255.0f, dp[LD.b] / 255.0f};
        // W3C: the source colour is mixed with its blend result where the backdrop exists.
        const Rgb bl = mix(mode, cb, cs);
        const Rgb csx{(1 - ab) * cs.r + ab * bl.r, (1 - ab) * cs.g + ab * bl.g, (1 - ab) * cs.b + ab * bl.b};
        const float ao = as + ab * (1 - as);
        const float inv = ao > 0 ? 1.0f / ao : 0.0f;
        const Rgb co{(as * csx.r + (1 - as) * ab * cb.r) * inv, (as * csx.g + (1 - as) * ab * cb.g) * inv,
                     (as * csx.b + (1 - as) * ab * cb.b) * inv};
        dp[LD.r] = toByte(co.r * 255.0f);
        dp[LD.g] = toByte(co.g * 255.0f);
        dp[LD.b] = toByte(co.b * 255.0f);
        if (LD.alpha >= 0) dp[LD.alpha] = toByte(ao * 255.0f);
      }
    }
  });
  return ok ? detail::ok() : detail::cancelledErr();
}

// ---- masks -------------------------------------------------------------------------

namespace {

// Running max (or min) over a window of radius r, O(1) per sample
// (van Herk / Gil-Werman). Edges extend the border value.
void runningExtreme(const uint8_t* in, int32_t n, int32_t stride, int32_t r, bool takeMax, uint8_t* out, int32_t outStride,
                    std::vector<uint8_t>& g, std::vector<uint8_t>& h) {
  const int32_t k = 2 * r + 1;
  const int32_t padded = n + 2 * r;
  const int32_t blocks = (padded + k - 1) / k;
  const int32_t len = blocks * k;
  g.assign(static_cast<std::size_t>(len), 0);
  h.assign(static_cast<std::size_t>(len), 0);
  auto val = [&](int32_t i) { return in[static_cast<std::size_t>(std::clamp(i - r, 0, n - 1)) * stride]; };
  auto pick = [takeMax](uint8_t a, uint8_t b) { return takeMax ? std::max(a, b) : std::min(a, b); };
  for (int32_t b = 0; b < blocks; ++b) {
    const int32_t s = b * k;
    g[s] = val(s);
    for (int32_t i = 1; i < k; ++i) g[s + i] = pick(g[s + i - 1], val(s + i));
    h[s + k - 1] = val(s + k - 1);
    for (int32_t i = k - 2; i >= 0; --i) h[s + i] = pick(h[s + i + 1], val(s + i));
  }
  for (int32_t x = 0; x < n; ++x) {
    // Window [x, x + 2r] in padded coordinates.
    out[static_cast<std::size_t>(x) * outStride] = pick(h[x], g[x + 2 * r]);
  }
}

Error grow(ImageBuffer& mask, int32_t r, bool takeMax, const CancellationToken* token) {
  const int32_t w = mask.width(), h = mask.height();
  auto tmp = ImageBuffer::allocate(w, h, PixelFormat::Gray8);
  if (!tmp) return tmp.error();
  ImageBuffer& t = tmp.value();
  bool ok = parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    std::vector<uint8_t> g, hh;
    for (int32_t y = y0; y < y1; ++y) runningExtreme(mask.row(y), w, 1, r, takeMax, t.row(y), 1, g, hh);
  });
  if (!ok) return detail::cancelledErr();
  ok = parallelFor(w, token, [&](int32_t x0, int32_t x1) {
    std::vector<uint8_t> g, hh;
    for (int32_t x = x0; x < x1; ++x) runningExtreme(t.row(0) + x, h, t.stride(), r, takeMax, mask.row(0) + x, mask.stride(), g, hh);
  });
  return ok ? detail::ok() : detail::cancelledErr();
}

}  // namespace

Error maskAdjust(ImageBuffer& mask, MaskOp op, float amount, const CancellationToken* token) {
  ScopedTimer timer("op.mask");
  if (mask.empty()) return detail::invalid("Empty mask");
  if (mask.format() != PixelFormat::Gray8) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Masks are gray images");
  if (!std::isfinite(amount)) return detail::invalid("Amount must be finite");
  switch (op) {
    case MaskOp::Invert:
      for (int32_t y = 0; y < mask.height(); ++y) {
        uint8_t* r = mask.row(y);
        for (int32_t x = 0; x < mask.width(); ++x) r[x] = static_cast<uint8_t>(255 - r[x]);
      }
      return detail::ok();
    case MaskOp::Threshold: {
      const int level = static_cast<int>(std::lround(std::clamp(amount, 0.0f, 1.0f) * 255.0f));
      for (int32_t y = 0; y < mask.height(); ++y) {
        uint8_t* r = mask.row(y);
        for (int32_t x = 0; x < mask.width(); ++x) r[x] = r[x] >= level ? 255 : 0;
      }
      return detail::ok();
    }
    case MaskOp::Feather: {
      auto soft = blur(mask, std::max(0.0f, amount), token);
      if (!soft) return soft.error();
      return copyPixels(soft.value(), mask);
    }
    case MaskOp::Grow: {
      const int32_t r = static_cast<int32_t>(std::lround(std::min(std::fabs(amount), 500.0f)));
      if (r == 0) return detail::ok();
      return grow(mask, r, amount > 0, token);
    }
  }
  return detail::invalid("Unknown mask operation");
}

Result<ImageBuffer> maskRefine(const ImageBuffer& mask, const ImageBuffer& guide, int32_t radius, float eps,
                               const CancellationToken* token) {
  ScopedTimer timer("op.maskRefine");
  if (mask.empty() || guide.empty()) return failure(detail::invalid("Empty image"));
  if (mask.format() != PixelFormat::Gray8) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Masks are gray images"));
  if (radius < 1 || radius > 512 || !std::isfinite(eps)) return failure(detail::invalid("Radius must be 1..512"));
  eps = std::clamp(eps, 1e-6f, 0.1f);
  const int32_t W = guide.width(), H = guide.height();
  // Fast guided filter: solve at a reduced size, apply at full size.
  const int32_t s = std::max(1, std::max(W, H) / 1024);
  const int32_t w = std::max(1, W / s), h = std::max(1, H / s);
  const int32_t r = std::max(1, radius / s);
  auto smallGuide = resize(guide, w, h, ResizeFilter::Area, token);
  if (!smallGuide) return failure(smallGuide.error());
  auto smallMask = resize(mask, w, h, mask.width() > w ? ResizeFilter::Area : ResizeFilter::Bilinear, token);
  if (!smallMask) return failure(smallMask.error());
  const Layout LG = layoutOf(guide.format());
  auto lumaAt = [&](const ImageBuffer& img, int32_t x, int32_t y) {
    const uint8_t* p = img.row(y) + x * LG.bpp;
    return (LG.colors == 1 ? p[0] : 0.299f * p[LG.r] + 0.587f * p[LG.g] + 0.114f * p[LG.b]) / 255.0f;
  };
  Plane planes[6];
  for (auto& p : planes) {
    auto a = Plane::allocate(w, h);
    if (!a) return failure(a.error());
    p = std::move(a).value();
  }
  Plane &I = planes[0], &P = planes[1], &II = planes[2], &IP = planes[3], &A = planes[4], &B = planes[5];
  for (int32_t y = 0; y < h; ++y) {
    for (int32_t x = 0; x < w; ++x) {
      const float i = lumaAt(smallGuide.value(), x, y), p = smallMask->row(y)[x] / 255.0f;
      I.row(y)[x] = i, P.row(y)[x] = p, II.row(y)[x] = i * i, IP.row(y)[x] = i * p;
    }
  }
  auto boxed = [&](Plane& p) -> Error {
    auto t = Plane::allocate(w, h);
    if (!t) return t.error();
    if (Error e = boxBlur(p, t.value(), r, token); e.status != PP_OK) return e;
    p = std::move(t).value();
    return detail::ok();
  };
  for (Plane* p : {&I, &P, &II, &IP}) {
    if (Error e = boxed(*p); e.status != PP_OK) return failure(e);
  }
  for (int32_t y = 0; y < h; ++y) {
    for (int32_t x = 0; x < w; ++x) {
      const float mi = I.row(y)[x], mp = P.row(y)[x];
      const float var = II.row(y)[x] - mi * mi, cov = IP.row(y)[x] - mi * mp;
      const float a = cov / (var + eps);
      A.row(y)[x] = a;
      B.row(y)[x] = mp - a * mi;
    }
  }
  if (Error e = boxed(A); e.status != PP_OK) return failure(e);
  if (Error e = boxed(B); e.status != PP_OK) return failure(e);
  auto out = ImageBuffer::allocate(W, H, PixelFormat::Gray8);
  if (!out) return failure(out.error());
  const float sx = static_cast<float>(w) / static_cast<float>(W), sy = static_cast<float>(h) / static_cast<float>(H);
  const bool ok = parallelFor(H, token, [&](int32_t y0, int32_t y1) {
    for (int32_t y = y0; y < y1; ++y) {
      uint8_t* d = out->row(y);
      const float fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
      for (int32_t x = 0; x < W; ++x) {
        const float fx = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
        const float q = A.sample(fx, fy) * lumaAt(guide, x, y) + B.sample(fx, fy);
        d[x] = toByte(q * 255.0f);
      }
    }
  });
  if (!ok) return failure(detail::cancelledErr());
  return std::move(out).value();
}

}  // namespace photoplus::ops
