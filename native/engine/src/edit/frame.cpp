#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

struct V2 {
  float x, y;
};

struct Colour {
  float r, g, b;
};
Colour colourOf(uint32_t argb) {
  return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
          static_cast<float>(argb & 0xFF) / 255.0f};
}
Colour mix(Colour a, Colour b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// Exact signed distance to a closed polygon (negative inside), after
// Inigo Quilez's sdPolygon.
float sdPolygon(const std::vector<V2>& v, V2 p) {
  float d = (p.x - v[0].x) * (p.x - v[0].x) + (p.y - v[0].y) * (p.y - v[0].y);
  float sign = 1.0f;
  for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i, ++i) {
    const V2 e{v[j].x - v[i].x, v[j].y - v[i].y};
    const V2 w{p.x - v[i].x, p.y - v[i].y};
    const float t = std::clamp((w.x * e.x + w.y * e.y) / (e.x * e.x + e.y * e.y), 0.0f, 1.0f);
    const V2 b{w.x - e.x * t, w.y - e.y * t};
    d = std::min(d, b.x * b.x + b.y * b.y);
    const bool c1 = p.y >= v[i].y, c2 = p.y < v[j].y, c3 = e.x * w.y > e.y * w.x;
    if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) sign = -sign;
  }
  return sign * std::sqrt(d);
}

// Polygon outline of a shape, fitted (aspect kept) into a box of half size
// (hw, hh) centred on the origin; y grows downwards.
std::vector<V2> polygonFor(Shape shape, float hw, float hh, float param) {
  std::vector<V2> v;
  constexpr float kPi = 3.14159265f;
  switch (shape) {
    case Shape::Star: {
      const float inner = std::clamp(param > 0.0f ? param : 0.47f, 0.25f, 0.8f);
      for (int i = 0; i < 10; ++i) {
        const float a = -kPi / 2.0f + static_cast<float>(i) * kPi / 5.0f;
        const float r = (i % 2 == 0) ? 1.0f : inner;
        v.push_back({r * std::cos(a), r * std::sin(a)});
      }
      break;
    }
    case Shape::Heart:
      for (int i = 0; i < 96; ++i) {
        const float t = static_cast<float>(i) / 96.0f * 2.0f * kPi;
        const float x = 16.0f * std::pow(std::sin(t), 3.0f);
        const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2 * t) - 2.0f * std::cos(3 * t) - std::cos(4 * t);
        v.push_back({x, -y});
      }
      break;
    case Shape::Hexagon:
      for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) * kPi / 3.0f;
        v.push_back({std::cos(a), std::sin(a)});
      }
      break;
    case Shape::Diamond:
      v = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
      break;
    default:
      break;
  }
  // Fit into the box, aspect kept, centred.
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (const auto& p : v) {
    minx = std::min(minx, p.x);
    maxx = std::max(maxx, p.x);
    miny = std::min(miny, p.y);
    maxy = std::max(maxy, p.y);
  }
  const float sx = 2.0f * hw / (maxx - minx), sy = 2.0f * hh / (maxy - miny);
  const float k = shape == Shape::Diamond ? 1.0f : std::min(sx, sy);
  const float kx = shape == Shape::Diamond ? sx : k, ky = shape == Shape::Diamond ? sy : k;
  const float mx = 0.5f * (minx + maxx), my = 0.5f * (miny + maxy);
  for (auto& p : v) p = {(p.x - mx) * kx, (p.y - my) * ky};
  return v;
}

float sdRoundRect(V2 p, float hw, float hh, float r) {
  const float qx = std::fabs(p.x) - (hw - r), qy = std::fabs(p.y) - (hh - r);
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

float sdShape(Shape shape, const std::vector<V2>& poly, V2 p, float hw, float hh, float param) {
  switch (shape) {
    case Shape::Rect:
      return sdRoundRect(p, hw, hh, std::clamp(param, 0.0f, 1.0f) * std::min(hw, hh));
    case Shape::Ellipse: {
      const float k = std::sqrt((p.x / hw) * (p.x / hw) + (p.y / hh) * (p.y / hh));
      return (k - 1.0f) * std::min(hw, hh);
    }
    case Shape::Arch: {
      // A rectangle whose top is a full semicircle.
      const float r = std::min(hw, hh);
      const float rect = sdRoundRect({p.x, p.y - 0.5f * r}, hw, hh - 0.5f * r, 0.0f);
      const float cy = -hh + r;
      const float circ = std::sqrt(p.x * p.x + (p.y - cy) * (p.y - cy)) - r;
      return std::min(rect, circ);
    }
    default:
      return sdPolygon(poly, p);
  }
}

}  // namespace

Error frame(const ImageBuffer& photo, ImageBuffer& canvas, const FrameParams& fp, const CancellationToken* token) {
  ScopedTimer timer("edit.frame");
  if (!isColour(photo) || !isColour(canvas)) return makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Frames need colour images");
  for (float v : {fp.shapeParam, fp.rotationDeg, fp.window[0], fp.window[1], fp.window[2], fp.window[3], fp.border,
                  fp.outlineGap, fp.outlineWidth, fp.shadow, fp.shadowOffset, fp.shadowBlur, fp.glow, fp.zoom, fp.offsetX,
                  fp.offsetY}) {
    if (!std::isfinite(v)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Frame parameters must be finite");
  }
  const auto shapeId = static_cast<int32_t>(fp.shape);
  if (shapeId < 0 || shapeId > 6) return makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown frame shape");
  if (fp.window[2] <= 0.02f || fp.window[3] <= 0.02f) return makeError(PP_ERROR_INVALID_ARGUMENT, "Frame window is too small");
  const int32_t CW = canvas.width(), CH = canvas.height();
  const float wx = fp.window[0] * static_cast<float>(CW), wy = fp.window[1] * static_cast<float>(CH);
  const float ww = fp.window[2] * static_cast<float>(CW), wh = fp.window[3] * static_cast<float>(CH);
  const float hw = 0.5f * ww, hh = 0.5f * wh, wcx = wx + hw, wcy = wy + hh;
  const float shortSide = std::min(ww, wh);
  const std::vector<V2> poly = polygonFor(fp.shape, hw, hh, fp.shapeParam);
  const float th = fp.rotationDeg * 3.14159265f / 180.0f, ct = std::cos(th), st = std::sin(th);
  auto local = [&](float x, float y) {  // canvas px -> window frame (unrotated, centred)
    const float dx = x - wcx, dy = y - wcy;
    return V2{dx * ct + dy * st, -dx * st + dy * ct};
  };

  // Signed distance on a grid (sampled bilinearly): exact near edges, and
  // cheap for shapes with many sides.
  const int32_t g = std::max(1, static_cast<int32_t>(shortSide / 400.0f));
  const int32_t gw = CW / g + 2, gh = CH / g + 2;
  auto sdf = Plane::allocate(gw, gh);
  if (!sdf) return sdf.error();
  const bool built = parallelFor(gh, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < gw; ++x)
        sdf->row(y)[x] = sdShape(fp.shape, poly,
                                 local((static_cast<float>(x) + 0.5f) * static_cast<float>(g) - 0.5f,
                                       (static_cast<float>(y) + 0.5f) * static_cast<float>(g) - 0.5f),
                                 hw, hh, fp.shapeParam);
  });
  if (!built) return cancelledError();
  const float invG = 1.0f / static_cast<float>(g);
  auto dist = [&](float x, float y) { return sdf->sample((x + 0.5f) * invG - 0.5f, (y + 0.5f) * invG - 0.5f); };

  // Blurred photo backdrop (cover fit), on a coarse grid.
  const Rgb po = offsets(photo), co = offsets(canvas);
  std::array<Plane, 3> back;
  const int32_t bd = std::max(1, std::max(CW, CH) / 160);
  if (fp.backdrop == Backdrop::BlurredPhoto) {
    const int32_t bw = CW / bd + 1, bh = CH / bd + 1;
    const float cover = std::max(static_cast<float>(CW) / static_cast<float>(photo.width()), static_cast<float>(CH) / static_cast<float>(photo.height()));
    for (size_t ch = 0; ch < 3; ++ch) {
      auto p = Plane::allocate(bw, bh), q = Plane::allocate(bw, bh);
      if (!p || !q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the frame");
      const int idx[3] = {po.r, po.g, po.b};
      for (int32_t y = 0; y < bh; ++y)
        for (int32_t x = 0; x < bw; ++x) {
          const float px = ((static_cast<float>(x) * static_cast<float>(bd) - 0.5f * static_cast<float>(CW)) / cover) + 0.5f * static_cast<float>(photo.width());
          const float py = ((static_cast<float>(y) * static_cast<float>(bd) - 0.5f * static_cast<float>(CH)) / cover) + 0.5f * static_cast<float>(photo.height());
          const int32_t sx = std::clamp(static_cast<int32_t>(px), 0, photo.width() - 1);
          const int32_t sy = std::clamp(static_cast<int32_t>(py), 0, photo.height() - 1);
          p->row(y)[x] = photo.row(sy)[static_cast<size_t>(sx) * 4 + idx[ch]] / 255.0f;
        }
      if (Error e = gaussianBlur(*p, *q, std::max(2.0f, 6.0f), token); e.status != PP_OK) return e;
      back[ch] = std::move(q).value();
    }
  }

  const Colour ca = colourOf(fp.colorA), cb = colourOf(fp.colorB), border = colourOf(fp.borderColor);
  const Colour outline = colourOf(fp.outlineColor), glow = colourOf(fp.glowColor);
  const float bwPx = fp.border * shortSide;
  const float gapPx = fp.outlineGap * shortSide, owPx = fp.outlineWidth * shortSide;
  const float shOff = fp.shadowOffset * shortSide, shBlur = std::max(1.0f, fp.shadowBlur * shortSide);
  const float glowR = 0.06f * shortSide;
  // Photo cover-fit inside the window, with zoom and pan.
  const float pw = static_cast<float>(photo.width()), ph = static_cast<float>(photo.height());
  const float sc = std::max(ww / pw, wh / ph) * std::max(1.0f, fp.zoom);
  const float slackX = pw * sc - ww, slackY = ph * sc - wh;
  const float panX = std::clamp(fp.offsetX, -1.0f, 1.0f) * 0.5f * slackX;
  const float panY = std::clamp(fp.offsetY, -1.0f, 1.0f) * 0.5f * slackY;
  auto photoAt = [&](V2 p, int idx) {
    const float fx = std::clamp((p.x + panX) / sc + 0.5f * pw - 0.5f, 0.0f, pw - 1.0f);
    const float fy = std::clamp((p.y + panY) / sc + 0.5f * ph - 0.5f, 0.0f, ph - 1.0f);
    const int32_t x0 = std::min(static_cast<int32_t>(fx), photo.width() - 2 < 0 ? 0 : photo.width() - 2);
    const int32_t y0 = std::min(static_cast<int32_t>(fy), photo.height() - 2 < 0 ? 0 : photo.height() - 2);
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const int nx = x0 + 1 < photo.width() ? 4 : 0;
    const uint8_t* r0 = photo.row(y0) + static_cast<size_t>(x0) * 4 + idx;
    const uint8_t* r1 = photo.row(std::min(y0 + 1, photo.height() - 1)) + static_cast<size_t>(x0) * 4 + idx;
    const float top = r0[0] + (static_cast<float>(r0[nx]) - r0[0]) * tx;
    const float bot = r1[0] + (static_cast<float>(r1[nx]) - r1[0]) * tx;
    return (top + (bot - top) * ty) / 255.0f;
  };
  // Film sprocket holes along the top and bottom margins.
  const float holeH = 0.35f * std::min(wy, static_cast<float>(CH) - (wy + wh));
  const float holeW = holeH * 1.3f, holePitch = holeW * 2.0f;

  const bool done = parallelFor(CH, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* out = canvas.row(y);
      const float fy = static_cast<float>(y);
      for (int32_t x = 0; x < CW; ++x) {
        const float fx = static_cast<float>(x);
        // Backdrop.
        Colour c;
        switch (fp.backdrop) {
          case Backdrop::Gradient:
            c = mix(ca, cb, fy / static_cast<float>(std::max(1, CH - 1)));
            break;
          case Backdrop::BlurredPhoto: {
            const float bx = fx / static_cast<float>(bd), by = fy / static_cast<float>(bd);
            c = {back[0].sample(bx, by), back[1].sample(bx, by), back[2].sample(bx, by)};
            const float l = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            c = mix(c, {l, l, l}, 0.15f);
            c = {c.r * 0.82f, c.g * 0.82f, c.b * 0.82f};
            break;
          }
          case Backdrop::Paper: {
            const float n = 0.012f * hashNoise(x, y, 0xBA9Eu) + 0.01f * hashNoise(x / 6, y / 6, 0xBA9Fu);
            const float nx = fx / static_cast<float>(CW) - 0.5f, ny = fy / static_cast<float>(CH) - 0.5f;
            const float v = 1.0f - 0.08f * (nx * nx + ny * ny) * 2.0f;
            c = {ca.r * v + n, ca.g * v + n, ca.b * v + n};
            break;
          }
          default:
            c = ca;
        }
        const float d = dist(fx, fy);
        // Glow around the shape.
        if (fp.glow > 0.0f && d > 0.0f) c = mix(c, glow, fp.glow * std::exp(-d / glowR) * 0.85f);
        // Shadow cast below the shape.
        if (fp.shadow > 0.0f) {
          const float ds = dist(fx, fy - shOff);
          const float a = fp.shadow * (1.0f - smoothstep(-shBlur, shBlur, ds)) * 0.6f * std::clamp(d + 0.5f, 0.0f, 1.0f);
          c = {c.r * (1.0f - a), c.g * (1.0f - a), c.b * (1.0f - a)};
        }
        // A thin outline floating around the shape.
        if (owPx > 0.0f) {
          const float od = d - gapPx;
          const float a = std::clamp(0.5f * owPx - std::fabs(od - 0.5f * owPx) + 0.5f, 0.0f, 1.0f);
          c = mix(c, outline, a);
        }
        // Film sprocket holes.
        if (fp.decoration == 1 && holeH > 1.0f && (fy < wy || fy > wy + wh)) {
          const float mid = fy < wy ? 0.5f * wy : 0.5f * (wy + wh + static_cast<float>(CH));
          const float lx = std::fmod(fx + 0.25f * holePitch, holePitch) - 0.5f * holePitch;
          const float hd = sdRoundRect({lx, fy - mid}, 0.5f * holeW, 0.5f * holeH, 0.25f * holeH);
          c = mix(c, {0.88f, 0.86f, 0.82f}, std::clamp(0.5f - hd, 0.0f, 1.0f));
        }
        // The photo inside the shape, with its border.
        const float inside = std::clamp(0.5f - d, 0.0f, 1.0f);
        if (inside > 0.0f) {
          const V2 p = local(fx, fy);
          Colour ph2{photoAt(p, po.r), photoAt(p, po.g), photoAt(p, po.b)};
          if (bwPx > 0.0f) {
            Colour bc = border;
            if (fp.metallicBorder) {
              const float s = 0.8f + 0.4f * (0.5f + 0.5f * std::sin(6.2832f * (0.6f * fx / static_cast<float>(CW) + 0.9f * fy / static_cast<float>(CH))));
              bc = {std::min(1.0f, bc.r * s), std::min(1.0f, bc.g * s), std::min(1.0f, bc.b * s)};
            }
            ph2 = mix(ph2, bc, std::clamp(d + bwPx + 0.5f, 0.0f, 1.0f));
          }
          c = mix(c, ph2, inside);
        }
        uint8_t* q = out + static_cast<size_t>(x) * 4;
        q[co.r] = static_cast<uint8_t>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
        q[co.g] = static_cast<uint8_t>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
        q[co.b] = static_cast<uint8_t>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
        q[3] = 255;
      }
    }
  });
  return done ? ok() : cancelledError();
}

}  // namespace photoplus::edit
