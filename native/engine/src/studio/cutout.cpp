// Studio cut-out: see studio.hpp. Layout of one run:
//   1. a working grid (≤ 1024 px, gamma colour) and the person mask on it,
//      cleared of stray islands;
//   2. the user's strokes painted on the grid: keep and remove are hard
//      constraints, "fix hair" marks a region where colour decides;
//   3. along the silhouette (narrowly), in hair zones (further out) and in
//      "fix hair" strokes, alpha from where each pixel sits between the
//      nearby person colour F and the nearby background colour B:
//      a = (I-B)·(F-B) / |F-B|², trusted only where F and B differ and the
//      pixel lies on the line between them; F, B and the band are kept so
//      the same model runs per pixel at full resolution;
//   4. colour guided-filter coefficients (He et al.) on the grid, evaluated
//      per pixel at full resolution, so edges follow the photo's own detail;
//   5. foreground colours by blur fusion (Forte 2021, an approximation of
//      Germer et al.'s multi-level estimation): smooth person and background
//      colours on the grid, the exact correction per pixel, so a hair strand
//      keeps its own colour and none of the old wall's.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "../matte/tools.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

using namespace matte;

namespace {

constexpr int32_t kGrid = 1024;

// Distance from p to segment ab.
inline float segDistance(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  const float vv = vx * vx + vy * vy;
  const float t = vv > 1e-12f ? std::clamp((wx * vx + wy * vy) / vv, 0.0f, 1.0f) : 0.0f;
  const float dx = wx - t * vx, dy = wy - t * vy;
  return std::sqrt(dx * dx + dy * dy);
}

// Where colour c sits between the person's colour f and the background's b
// (I = aF + (1-a)B, solved along the F–B line). `conf` is how far the two
// are apart and how well c lies on the line between them: a third colour
// (a lamp, a pattern) is neither, so it is not trusted.
inline float twoColourAlpha(V3 c, V3 f, V3 b, float& conf) {
  const V3 d = f - b, e = c - b;
  const float dd = d.r * d.r + d.g * d.g + d.b * d.b;
  const float sep = std::sqrt(dd);
  conf = smoothstep(0.05f, 0.14f, sep);
  if (conf <= 0.0f) return 0.0f;
  const float k = (e.r * d.r + e.g * d.g + e.b * d.b) / std::max(dd, 1e-6f);
  const float a = std::clamp(k, 0.0f, 1.0f);
  const V3 off = e - d * a;
  const float stray = std::sqrt(off.r * off.r + off.g * off.g + off.b * off.b) / std::max(sep, 1e-3f);
  conf *= 1.0f - smoothstep(0.3f, 0.65f, stray);
  return a;
}

struct StrokePlanes {
  Plane keep, remove, hair;
  bool any = false;
};

// Takes the old background's light out of an edge colour c. Where c leans
// toward the background's tint b further than the person's own colour f
// (from further inside, at c's brightness) does, it moves to that own
// colour, and most of the brightness the old light added goes too (a
// sunset's glow on hair). Skin beside a warm wall leans no further than
// the skin itself does, so it stays; a grey or white background has no
// tint, so nothing changes.
// `band` (0..1) is how much of the edge this pixel is.
V3 despill(V3 c, V3 f, V3 b, float band) {
  const float lb = lum(b);
  const V3 tint = b - grey(lb);
  const float tn = std::sqrt(tint.r * tint.r + tint.g * tint.g + tint.b * tint.b);
  const float mxb = std::max({b.r, b.g, b.b}), sat = mxb > 1e-3f ? (mxb - std::min({b.r, b.g, b.b})) / mxb : 0.0f;
  const float k = band * smoothstep(0.04f, 0.2f, sat);
  if (k <= 0.0f || tn < 1e-4f) return c;
  const float lc = lum(c);
  // The person's own colour at this pixel's brightness.
  const float lf = std::max(lum(f), 1e-4f);
  // Never brighter than 1.2 times the person's own: hair lit gold by a
  // sunset is compared with how dark the hair really is.
  V3 own = f * std::min(lc / lf, 1.2f);
  const float mo = std::max({own.r, own.g, own.b});
  if (mo > 1.0f) own = own * (1.0f / mo);
  // How far the edge leans toward the old background's tint beyond that:
  // skin beside a warm wall is still skin, and stays.
  const V3 d = c - own;
  const float along = (d.r * tint.r + d.g * tint.g + d.b * tint.b) / tn;
  // Or it is far brighter than the person's own colour, tinted like the
  // old background, with a bright background behind it: a sunset's glow
  // through hair. (Skin in light beside a warm wall is a little brighter
  // than the skin, not three times.)
  const V3 ct = c - grey(lc);
  const float cn = std::sqrt(ct.r * ct.r + ct.g * ct.g + ct.b * ct.b);
  const float same = cn > 1e-4f ? (ct.r * tint.r + ct.g * tint.g + ct.b * tint.b) / (cn * tn) : 0.0f;
  const float bright = smoothstep(1.5f, 2.5f, lc / lf) * smoothstep(0.3f, 0.7f, lb) * smoothstep(0.3f, 0.7f, same);
  // Only the light changes, never what the thing is: an edge whose hue is
  // far from its surface's own (a gold brooch the parts map calls part of
  // a green dress) is not that surface, and is left alone.
  const V3 ot = own - grey(lum(own));
  const float on = std::sqrt(ot.r * ot.r + ot.g * ot.g + ot.b * ot.b);
  const float hueOk = (on < 0.02f || cn < 0.02f) ? 1.0f : smoothstep(0.3f, 0.7f, (ct.r * ot.r + ct.g * ot.g + ct.b * ot.b) / (cn * on));
  const float w = k * hueOk * std::max(smoothstep(0.03f, 0.12f, along), bright);
  if (w <= 0.0f) return c;
  V3 out = mix(c, own, w);
  // The brightness the old light added, mostly taken out.
  const float lo = lum(out);
  if (lo > lf && lo > 1e-4f) out = out * mixf(1.0f, lf / lo, 0.8f * w * smoothstep(0.3f, 0.7f, lb));
  return V3{std::clamp(out.r, 0.0f, 1.0f), std::clamp(out.g, 0.0f, 1.0f), std::clamp(out.b, 0.0f, 1.0f)};
}

// Strokes painted in order on the grid (a later stroke wins where they
// overlap), each with a one-pixel soft edge.
Result<StrokePlanes> paintStrokes(const std::vector<Stroke>& strokes, int32_t w, int32_t h) {
  StrokePlanes s;
  auto k = Plane::allocate(w, h), r = Plane::allocate(w, h), u = Plane::allocate(w, h);
  if (!k || !r || !u) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out"));
  s.keep = std::move(k).value(), s.remove = std::move(r).value(), s.hair = std::move(u).value();
  std::fill(s.keep.data(), s.keep.data() + s.keep.size(), 0.0f);
  std::fill(s.remove.data(), s.remove.data() + s.remove.size(), 0.0f);
  std::fill(s.hair.data(), s.hair.data() + s.hair.size(), 0.0f);
  const float shortSide = static_cast<float>(std::min(w, h));
  for (const Stroke& st : strokes) {
    if (st.points.empty() || !std::isfinite(st.radius)) continue;
    const float rad = std::clamp(st.radius, 0.002f, 0.5f) * shortSide;
    Plane* paint = st.brush == Brush::Keep ? &s.keep : st.brush == Brush::Remove ? &s.remove : &s.hair;
    Plane* others[2] = {st.brush == Brush::Keep ? &s.remove : &s.keep, st.brush == Brush::Hair ? &s.remove : &s.hair};
    std::vector<std::array<float, 2>> pts;
    pts.reserve(st.points.size());
    for (const auto& p : st.points) {
      if (!std::isfinite(p[0]) || !std::isfinite(p[1])) continue;
      pts.push_back({p[0] * static_cast<float>(w) - 0.5f, p[1] * static_cast<float>(h) - 0.5f});
    }
    if (pts.empty()) continue;
    if (pts.size() == 1) pts.push_back(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) {
      const auto& a = pts[i - 1];
      const auto& b = pts[i];
      const int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(std::min(a[0], b[0]) - rad - 1)));
      const int32_t x1 = std::min(w - 1, static_cast<int32_t>(std::ceil(std::max(a[0], b[0]) + rad + 1)));
      const int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(std::min(a[1], b[1]) - rad - 1)));
      const int32_t y1 = std::min(h - 1, static_cast<int32_t>(std::ceil(std::max(a[1], b[1]) + rad + 1)));
      for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x) {
          const float d = segDistance(static_cast<float>(x), static_cast<float>(y), a[0], a[1], b[0], b[1]);
          const float v = std::clamp(rad + 0.5f - d, 0.0f, 1.0f);
          if (v <= 0.0f) continue;
          float& p = paint->row(y)[x];
          p = std::max(p, v);
          for (Plane* o : others) o->row(y)[x] *= 1.0f - v;
        }
    }
    s.any = true;
  }
  return s;
}

// One class of the parts map as a soft plane on the grid.
Result<Plane> zonePlane(const ImageBuffer& zones, Zone zone, int32_t w, int32_t h, const CancellationToken* token) {
  auto z = Plane::allocate(zones.width(), zones.height());
  if (!z) return z;
  const int32_t bpp = bytesPerPixel(zones.format());
  const auto id = static_cast<uint8_t>(zone);
  for (int32_t y = 0; y < zones.height(); ++y) {
    const uint8_t* src = zones.row(y);
    for (int32_t x = 0; x < zones.width(); ++x) z->row(y)[x] = src[x * bpp] == id ? 1.0f : 0.0f;
  }
  return resizeBilinear(*z, w, h, token);
}

}  // namespace

// The faces of the people kept: face-skin pixels of the parts map that are
// inside the cut-out (so faces of people left out don't count), split into
// separate faces (connected pieces). The largest face gives the size; its
// box is taken between the 2nd and 98th percentiles so stray pixels don't
// stretch it. One box around a couple's two faces would be as tall as the
// difference in their heights, and would make them look far too small.
namespace {
void faceBox(const ImageBuffer& zones, const ImageBuffer& out, CutoutReport& report) {
  const int32_t zw = zones.width(), zh = zones.height(), W = out.width(), H = out.height();
  const int32_t bpp = bytesPerPixel(zones.format());
  const auto face = static_cast<uint8_t>(Zone::FaceSkin);
  std::vector<uint8_t> on(static_cast<size_t>(zw) * static_cast<size_t>(zh), 0);
  size_t total = 0;
  for (int32_t y = 0; y < zh; ++y) {
    const uint8_t* src = zones.row(y);
    const int32_t oy = std::min(H - 1, static_cast<int32_t>((static_cast<int64_t>(y) * 2 + 1) * H / (2 * zh)));
    const uint8_t* o = out.row(oy);
    for (int32_t x = 0; x < zw; ++x) {
      if (src[x * bpp] != face) continue;
      const int32_t ox = std::min(W - 1, static_cast<int32_t>((static_cast<int64_t>(x) * 2 + 1) * W / (2 * zw)));
      if (o[ox * 4 + 3] > 127) on[static_cast<size_t>(y) * zw + x] = 1, ++total;
    }
  }
  // A face of a few pixels says little about size.
  if (total < 24) return;
  // Connected pieces (8-neighbour), each a list of pixels.
  std::vector<int32_t> label(on.size(), -1);
  std::vector<std::vector<int32_t>> pieces;
  std::vector<int32_t> stack;
  for (size_t i = 0; i < on.size(); ++i) {
    if (!on[i] || label[i] >= 0) continue;
    const auto id = static_cast<int32_t>(pieces.size());
    pieces.emplace_back();
    stack.assign(1, static_cast<int32_t>(i));
    label[i] = id;
    while (!stack.empty()) {
      const int32_t k = stack.back();
      stack.pop_back();
      pieces.back().push_back(k);
      const int32_t x = k % zw, y = k / zw;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int32_t nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= zw || ny >= zh) continue;
          const auto n = static_cast<size_t>(ny) * zw + nx;
          if (on[n] && label[n] < 0) label[n] = id, stack.push_back(static_cast<int32_t>(n));
        }
    }
  }
  size_t largest = 0;
  for (size_t i = 1; i < pieces.size(); ++i)
    if (pieces[i].size() > pieces[largest].size()) largest = i;
  const auto& big = pieces[largest];
  if (big.size() < 24) return;
  // Faces: pieces at least a fifth the size of the largest (an ear or a hand
  // that reads as face skin is smaller).
  int32_t faces = 0;
  for (const auto& p : pieces)
    if (p.size() >= std::max<size_t>(24, big.size() / 5)) ++faces;
  std::vector<int32_t> xs, ys;
  xs.reserve(big.size()), ys.reserve(big.size());
  for (int32_t k : big) xs.push_back(k % zw), ys.push_back(k / zw);
  auto pct = [](std::vector<int32_t>& v, double q) {
    const auto k = static_cast<size_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
  };
  const float l = static_cast<float>(pct(xs, 0.02)), r = static_cast<float>(pct(xs, 0.98) + 1);
  const float t = static_cast<float>(pct(ys, 0.02)), b = static_cast<float>(pct(ys, 0.98) + 1);
  report.face = true;
  report.faces = faces;
  report.faceLeft = l / static_cast<float>(zw), report.faceRight = r / static_cast<float>(zw);
  report.faceTop = t / static_cast<float>(zh), report.faceBottom = b / static_cast<float>(zh);
}

// The outfit's main colours: the cut-out's own colours where the parts map
// says clothes, grouped in a coarse colour histogram (16 levels a channel).
// The fullest group with its neighbours is the first colour (its mean); its
// pixels are then set aside and the second is found the same way, if it
// covers at least a sixth of the clothes.
void outfitColours(const ImageBuffer& zones, const ImageBuffer& out, CutoutReport& report) {
  const int32_t zw = zones.width(), zh = zones.height(), W = out.width(), H = out.height();
  const int32_t bpp = bytesPerPixel(zones.format());
  const Rgb o = offsets(out);
  const auto clothes = static_cast<uint8_t>(Zone::Clothes);
  std::vector<std::array<uint8_t, 3>> px;
  for (int32_t y = 0; y < zh; ++y) {
    const uint8_t* src = zones.row(y);
    const int32_t oy = std::min(H - 1, static_cast<int32_t>((static_cast<int64_t>(y) * 2 + 1) * H / (2 * zh)));
    const uint8_t* row = out.row(oy);
    for (int32_t x = 0; x < zw; ++x) {
      if (src[x * bpp] != clothes) continue;
      const int32_t ox = std::min(W - 1, static_cast<int32_t>((static_cast<int64_t>(x) * 2 + 1) * W / (2 * zw)));
      const uint8_t* p = row + ox * 4;
      if (p[3] > 200) px.push_back({p[o.r], p[o.g], p[o.b]});
    }
  }
  if (px.size() < 50) return;
  std::vector<uint8_t> used(px.size(), 0);
  auto bin = [](const std::array<uint8_t, 3>& c) { return (c[0] >> 4) * 256 + (c[1] >> 4) * 16 + (c[2] >> 4); };
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<int32_t> hist(4096, 0);
    size_t left = 0;
    for (size_t i = 0; i < px.size(); ++i)
      if (!used[i]) ++hist[static_cast<size_t>(bin(px[i]))], ++left;
    if (left == 0) break;
    int32_t best = -1, bestCount = 0;
    for (int32_t r = 0; r < 16; ++r)
      for (int32_t g = 0; g < 16; ++g)
        for (int32_t b = 0; b < 16; ++b) {
          int32_t c = 0;
          for (int32_t dr = -1; dr <= 1; ++dr)
            for (int32_t dg = -1; dg <= 1; ++dg)
              for (int32_t db = -1; db <= 1; ++db) {
                const int32_t rr = r + dr, gg = g + dg, bb = b + db;
                if (rr < 0 || gg < 0 || bb < 0 || rr > 15 || gg > 15 || bb > 15) continue;
                c += hist[static_cast<size_t>(rr * 256 + gg * 16 + bb)];
              }
          if (c > bestCount) bestCount = c, best = r * 256 + g * 16 + b;
        }
    if (best < 0) break;
    const int32_t br = best / 256, bg = (best / 16) % 16, bb = best % 16;
    double sr = 0, sg = 0, sb = 0, n = 0;
    for (size_t i = 0; i < px.size(); ++i) {
      if (used[i]) continue;
      const auto& c = px[i];
      if (std::abs((c[0] >> 4) - br) <= 1 && std::abs((c[1] >> 4) - bg) <= 1 && std::abs((c[2] >> 4) - bb) <= 1) {
        sr += c[0], sg += c[1], sb += c[2], n += 1, used[i] = 1;
      }
    }
    const float share = static_cast<float>(n / static_cast<double>(px.size()));
    if (n < 1 || (pass == 1 && share < 1.0f / 6.0f)) break;
    report.outfit[static_cast<size_t>(pass)] = {static_cast<float>(sr / n), static_cast<float>(sg / n), static_cast<float>(sb / n)};
    report.outfitShare[static_cast<size_t>(pass)] = share;
    report.outfitCount = pass + 1;
  }
}
}  // namespace

Error cutout(const ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* zones,
             const std::vector<Stroke>& strokes, const CutoutParams& params, ImageBuffer& out,
             const CancellationToken* token, CutoutReport* report) {
  ScopedTimer timer("studio.cutout");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "The cut-out needs an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  if (subject.empty() || subject.width() < 2 || subject.height() < 2) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Person mask is required");
  }
  if (out.width() != image.width() || out.height() != image.height() || out.format() != image.format()) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "The cut-out must be the photo's size and format");
  }
  if (zones != nullptr && (zones->empty() || zones->width() < 2 || zones->height() < 2)) zones = nullptr;
  if (!std::isfinite(params.softness) || !std::isfinite(params.shift) || !std::isfinite(params.hairDetail)) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad cut-out settings");
  }
  const float softness = std::clamp(params.softness, 0.0f, 1.0f);
  const float shift = std::clamp(params.shift, -1.0f, 1.0f);
  const float hairDetail = std::clamp(params.hairDetail, 0.0f, 1.0f);
  if (report != nullptr) *report = CutoutReport{};
  const int32_t W = image.width(), H = image.height();
  const Rgb o = offsets(image);

  // 1. Working grid.
  const int32_t f = std::max<int32_t>(1, (std::max(W, H) + kGrid - 1) / kGrid);
  const int32_t lw = (W + f - 1) / f, lh = (H + f - 1) / f;
  const float shortSide = static_cast<float>(std::min(lw, lh));
  auto gamR = allocRgb(lw, lh);
  if (!gamR) return gamR.error();
  Rgb3 gam = std::move(gamR).value();
  {
    const bool done = parallelFor(lh, token, [&](int32_t begin, int32_t end) {
      for (int32_t ly = begin; ly < end; ++ly)
        for (int32_t lx = 0; lx < lw; ++lx) {
          float sr = 0, sg = 0, sb = 0;
          int32_t n = 0;
          for (int32_t y = ly * f; y < std::min(H, (ly + 1) * f); ++y) {
            const uint8_t* row = image.row(y);
            for (int32_t x = lx * f; x < std::min(W, (lx + 1) * f); ++x, ++n) {
              const uint8_t* q = row + x * 4;
              sr += q[o.r], sg += q[o.g], sb += q[o.b];
            }
          }
          const float k = n ? 1.0f / (255.0f * static_cast<float>(n)) : 0.0f;
          gam.r.row(ly)[lx] = sr * k, gam.g.row(ly)[lx] = sg * k, gam.b.row(ly)[lx] = sb * k;
        }
    });
    if (!done) return cancelledError();
  }
  auto coarse0 = maskPlane(subject);
  if (!coarse0) return coarse0.error();
  auto coarseR = resizeBilinear(*coarse0, lw, lh, token);
  if (!coarseR) return coarseR.error();
  Plane mask = std::move(coarseR).value();
  // Person models leave a faint haze of "maybe" over the whole background
  // (a few percent); kept, it would be a veil of the old room in a
  // transparent cut-out, and nothing would count as clearly background.
  for (size_t i = 0; i < mask.size(); ++i) mask.data()[i] = smoothstep(0.1f, 0.9f, mask.data()[i]);
  if (Error e = dropIslands(mask, token); e.status != PP_OK) return e;

  // 2. Strokes.
  auto strokesR = paintStrokes(strokes, lw, lh);
  if (!strokesR) return strokesR.error();
  StrokePlanes& sp = *strokesR;
  if (sp.any) {
    for (size_t i = 0; i < mask.size(); ++i) {
      float& m = mask.data()[i];
      m = std::min(std::max(m, sp.keep.data()[i]), 1.0f - sp.remove.data()[i]);
    }
  }

  // Snap: a person model working at a few hundred pixels puts the edge
  // several pixels off. A wide colour guided filter moves it onto the
  // photo's own edge (within ~1.5% of the short side); the steps below then
  // refine it.
  {
    std::array<Plane, 3> sA;
    for (auto& p : sA) {
      auto q = Plane::allocate(lw, lh);
      if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
      p = std::move(q).value();
    }
    auto sB = Plane::allocate(lw, lh);
    if (!sB) return sB.error();
    const int32_t r1 = std::max<int32_t>(2, static_cast<int32_t>(std::lround(0.015f * shortSide)));
    if (Error e = guidedCoefficientsColor({&gam.r, &gam.g, &gam.b}, mask, r1, 4e-3f, sA, *sB, token); e.status != PP_OK) return e;
    // How well the photo's colours explain the mask nearby: where the person
    // and the room beside them are the same colour (dark hair against a dark
    // beam), the fit cannot tell them apart and would cut along whatever edge
    // the room has (a beam's straight line across the top of the head). There
    // the model's own edge is kept.
    auto fit = Plane::allocate(lw, lh), err = Plane::allocate(lw, lh), sq = Plane::allocate(lw, lh);
    if (!fit || !err || !sq) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    for (size_t i = 0; i < mask.size(); ++i) {
      const float v = sA[0].data()[i] * gam.r.data()[i] + sA[1].data()[i] * gam.g.data()[i] + sA[2].data()[i] * gam.b.data()[i] + sB->data()[i];
      fit->data()[i] = v;
      const float d = v - mask.data()[i];
      err->data()[i] = d * d;
      sq->data()[i] = mask.data()[i] * mask.data()[i];
    }
    const float rs = static_cast<float>(r1);
    auto errB = blurred(*err, rs, token), meanB = blurred(mask, rs, token), sqB = blurred(*sq, rs, token);
    if (!errB || !meanB || !sqB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    for (size_t i = 0; i < mask.size(); ++i) {
      const float mean = meanB->data()[i];
      const float var = std::max(sqB->data()[i] - mean * mean, 0.0f);
      // Share of the mask's variation nearby the colours leave unexplained.
      const float unexplained = var > 1e-3f ? errB->data()[i] / var : 0.0f;
      const float trust = 1.0f - smoothstep(0.25f, 0.6f, unexplained);
      float m = mask.data()[i] + (smoothstep(0.2f, 0.8f, fit->data()[i]) - mask.data()[i]) * trust;
      if (sp.any) m = std::min(std::max(m, sp.keep.data()[i]), 1.0f - sp.remove.data()[i]);
      mask.data()[i] = m;
    }
  }

  // Hair zone (the parts map is coarse: hair strands run past it).
  Plane hairZone, hairCore;
  bool haveHair = false;
  if (zones != nullptr) {
    auto hz = zonePlane(*zones, Zone::Hair, lw, lh, token);
    if (!hz) return hz.error();
    auto wide = blurred(*hz, 0.01f * shortSide, token);
    if (!wide) return wide.error();
    for (size_t i = 0; i < wide->size(); ++i) wide->data()[i] = std::max(hz->data()[i], smoothstep(0.05f, 0.3f, wide->data()[i]));
    hairZone = std::move(wide).value();
    hairCore = std::move(hz).value();
    haveHair = true;
  }

  // 3. Edges where colour decides.
  // Where colour decides: the hair band (hair zone near the silhouette) and
  // "fix hair" strokes. There each pixel is a mix of the nearby hair colour
  // F and the nearby background B: alpha from where it sits between them,
  // and its own colour unmixed (I = aF + (1-a)B). F, B and the band are kept
  // for the full-resolution pass, so curls and the gaps between them stay
  // sharp instead of being averaged on the grid.
  Rgb3 hairF, hairB;
  Plane hairD;
  bool twoColour = false;
  // The person model's own outline, kept: where the person and the room
  // beside them are the same colour (brown skin before a brown wall), colour
  // can't place the edge, and the model's outline is the better guess.
  auto modelR = blurred(mask, std::max(0.75f, 0.0015f * shortSide), token);
  if (!modelR) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
  Plane modelMask = std::move(modelR).value();
  // The model's mask is coarse and soft: a crisp edge at its middle.
  for (size_t i = 0; i < modelMask.size(); ++i) modelMask.data()[i] = smoothstep(0.38f, 0.62f, modelMask.data()[i]);
  {
    auto reach = blurred(mask, 0.012f * shortSide, token);
    auto reachW = blurred(mask, 0.016f * shortSide, token);
    auto reachN = blurred(mask, 0.006f * shortSide, token);
    auto inner = blurred(mask, 0.006f * shortSide, token);
    if (!reach || !reachW || !reachN || !inner) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    auto decide = Plane::allocate(lw, lh);
    auto wF = Plane::allocate(lw, lh), wH = Plane::allocate(lw, lh), wB = Plane::allocate(lw, lh);
    if (!decide || !wF || !wH || !wB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    constexpr float edgeWeight = 1.0f;
    bool anyDecide = false;
    for (size_t i = 0; i < mask.size(); ++i) {
      // Hair: a band reaching past the mask, where the parts map sees hair.
      const float bandH = smoothstep(0.02f, 0.1f, reachW->data()[i]) * (1.0f - smoothstep(0.9f, 0.995f, inner->data()[i]));
      // Every other edge: a narrow band on the mask's own boundary.
      const float rn = reachN->data()[i];
      const float bandE = smoothstep(0.02f, 0.2f, rn) * (1.0f - smoothstep(0.8f, 0.98f, rn));
      const float hz = haveHair ? hairZone.data()[i] * hairDetail : 0.0f;
      const float uh = sp.any ? sp.hair.data()[i] : 0.0f;
      const float d = std::max({bandH * hz, bandE * edgeWeight, uh});
      decide->data()[i] = d;
      anyDecide = anyDecide || d > 0.0f;
      const float in = smoothstep(0.9f, 0.99f, inner->data()[i]) * (1.0f - uh);
      wF->data()[i] = in;
      wH->data()[i] = in * (haveHair ? 0.04f + 0.96f * hairCore.data()[i] : 1.0f);  // hair itself
      wB->data()[i] = std::max(1.0f - smoothstep(0.004f, 0.04f, reach->data()[i]), sp.any ? sp.remove.data()[i] : 0.0f) * (1.0f - uh);
    }
    if (anyDecide) {
      auto Fwide = weightedPlate(gam, *wF, 0.07f * shortSide, grey(0.2f), token);
      if (!Fwide) return Fwide.error();
      auto F = weightedPlate(gam, *wF, 0.015f * shortSide, grey(0.2f), token, 0, &*Fwide);
      if (!F) return F.error();
      if (haveHair) {
        auto Hc = weightedPlate(gam, *wH, 0.02f * shortSide, grey(0.2f), token, 0, &*Fwide);
        if (!Hc) return Hc.error();
        for (size_t i = 0; i < mask.size(); ++i) F->set(i, mix(F->at(i), Hc->at(i), hairZone.data()[i]));
      }
      auto Bwide = weightedPlate(gam, *wB, 0.12f * shortSide, grey(0.5f), token);
      if (!Bwide) return Bwide.error();
      auto B = weightedPlate(gam, *wB, 0.03f * shortSide, grey(0.5f), token, 0, &*Bwide);
      if (!B) return B.error();
      for (size_t i = 0; i < mask.size(); ++i) {
        const float d = decide->data()[i];
        if (d <= 0.0f) continue;
        float conf = 0.0f;
        const float a = twoColourAlpha(gam.at(i), F->at(i), B->at(i), conf);
        if (conf <= 0.0f) continue;
        float& m = mask.data()[i];
        m += (a - m) * d * conf;
      }
      hairF = std::move(F).value(), hairB = std::move(B).value(), hairD = std::move(decide).value();
      twoColour = true;
    }
  }
  if (sp.any) {
    for (size_t i = 0; i < mask.size(); ++i) {
      float& m = mask.data()[i];
      m = std::min(std::max(m, sp.keep.data()[i]), 1.0f - sp.remove.data()[i]);
    }
  }

  // 4. Guided-filter coefficients on the grid.
  std::array<Plane, 3> mA;
  for (auto& p : mA) {
    auto q = Plane::allocate(lw, lh);
    if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    p = std::move(q).value();
  }
  auto mBR = Plane::allocate(lw, lh);
  if (!mBR) return mBR.error();
  Plane& mB = *mBR;
  const int32_t gr = std::max<int32_t>(2, static_cast<int32_t>(shortSide) / 200);
  if (Error e = guidedCoefficientsColor({&gam.r, &gam.g, &gam.b}, mask, gr, 1e-3f, mA, mB, token); e.status != PP_OK) return e;
  // How much colour may say where the edge is: where the person's and the
  // room's colours are far apart; hair keeps colour's say (strands need it).
  Plane colourSays;
  if (twoColour) {
    auto cs = Plane::allocate(lw, lh);
    if (!cs) return cs.error();
    for (size_t i = 0; i < cs->size(); ++i) {
      const V3 df = hairF.at(i) - hairB.at(i);
      const float apart = std::sqrt(df.r * df.r + df.g * df.g + df.b * df.b);
      const float hz = haveHair ? hairZone.data()[i] : 0.0f;
      cs->data()[i] = std::max(smoothstep(0.1f, 0.22f, apart), hz);
    }
    colourSays = std::move(cs).value();
  }
  auto alphaR = Plane::allocate(lw, lh);
  if (!alphaR) return alphaR.error();
  Plane& A = *alphaR;
  for (size_t i = 0; i < A.size(); ++i) {
    float a = mA[0].data()[i] * gam.r.data()[i] + mA[1].data()[i] * gam.g.data()[i] + mA[2].data()[i] * gam.b.data()[i] + mB.data()[i];
    a = std::clamp(a, 0.0f, 1.0f);
    if (twoColour) a = modelMask.data()[i] + (a - modelMask.data()[i]) * colourSays.data()[i];
    if (sp.any) a = std::min(std::max(a, sp.keep.data()[i]), 1.0f - sp.remove.data()[i]);
    A.data()[i] = a;
  }
  // Softer / shifted edges come from a blurred view of the matte.
  Plane soft, shifted;
  if (softness > 0.0f) {
    auto s = blurred(A, (0.0015f + 0.006f * softness) * shortSide, token);
    if (!s) return s.error();
    soft = std::move(s).value();
  }
  if (shift != 0.0f) {
    auto s = blurred(A, (0.002f + 0.006f * std::fabs(shift)) * shortSide, token);
    if (!s) return s.error();
    shifted = std::move(s).value();
  }

  // 5. Foreground colours (blur fusion), on the grid.
  Rgb3 Fs, Bs, Bw, spillOwn;
  Plane spillSure;
  bool spill = false;
  Plane rim;
  const bool clean = params.cleanColours;
  if (clean) {
    auto wa = clonePlane(A);
    auto wb = Plane::allocate(lw, lh);
    if (!wa || !wb) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    for (size_t i = 0; i < A.size(); ++i) wb->data()[i] = 1.0f - A.data()[i];
    // Stage 1: wide.
    auto F1 = weightedPlate(gam, *wa, 0.05f * shortSide, grey(0.5f), token);
    auto B1 = weightedPlate(gam, *wb, 0.05f * shortSide, grey(0.5f), token);
    if (!F1 || !B1) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    for (size_t i = 0; i < A.size(); ++i) {
      const float a = A.data()[i];
      const V3 c = gam.at(i), fh = F1->at(i), bh = B1->at(i);
      const V3 fc = fh + (c - fh * a - bh * (1.0f - a)) * a;
      F1->set(i, V3{std::clamp(fc.r, 0.0f, 1.0f), std::clamp(fc.g, 0.0f, 1.0f), std::clamp(fc.b, 0.0f, 1.0f)});
    }
    // Stage 2: close.
    auto F2 = weightedPlate(*F1, *wa, std::max(1.0f, 0.004f * shortSide), grey(0.5f), token);
    auto B2 = weightedPlate(*B1, *wb, std::max(1.0f, 0.004f * shortSide), grey(0.5f), token);
    if (!F2 || !B2) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    Fs = std::move(F2).value(), Bs = std::move(B2).value(), Bw = std::move(B1).value();
    // Old light on the edges (spill): a coloured background (a sunset, a
    // lamp, a green wall) lights the hair and the outline in its colour,
    // and that colour would travel with the person into a new scene. Each
    // edge pixel is compared with its own kind of surface further inside
    // (hair with hair, skin with skin, clothes with clothes, from the parts
    // map: sunlit skin compared with a green dress would turn green), in a
    // band along the outline. Without a parts map nothing is changed.
    if (zones != nullptr) {
      auto blurA = blurred(A, 0.03f * shortSide, token);
      auto wi = Plane::allocate(lw, lh);
      auto rp = Plane::allocate(lw, lh);
      auto face = zonePlane(*zones, Zone::FaceSkin, lw, lh, token);
      auto bodySkin = zonePlane(*zones, Zone::BodySkin, lw, lh, token);
      auto cloth = zonePlane(*zones, Zone::Clothes, lw, lh, token);
      auto accessory = zonePlane(*zones, Zone::Accessory, lw, lh, token);
      auto ownR = allocRgb(lw, lh);
      auto sureP = Plane::allocate(lw, lh);
      if (!blurA || !wi || !rp || !face || !bodySkin || !cloth || !accessory || !ownR || !sureP) {
        return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
      }
      for (size_t i = 0; i < A.size(); ++i) {
        const float ba = blurA->data()[i];
        wi->data()[i] = smoothstep(0.97f, 1.0f, ba) * A.data()[i];
        rp->data()[i] = A.data()[i] > 0.0f ? 1.0f - smoothstep(0.72f, 0.99f, ba) : 0.0f;
        bodySkin->data()[i] = std::min(1.0f, bodySkin->data()[i] + face->data()[i]);
      }
      auto rimB = blurred(*rp, std::max(1.0f, 0.004f * shortSide), token);
      if (!rimB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
      rim = std::move(rimB).value();
      // Each kind of surface: its colour away from the outline (wide, so the
      // side lit by the old light takes the colour of the rest), and how
      // much of it there is around to measure.
      const Plane* part[3] = {haveHair ? &hairZone : nullptr, &bodySkin.value(), &cloth.value()};
      const float sigma[3] = {0.15f * shortSide, 0.1f * shortSide, 0.1f * shortSide};
      for (size_t i = 0; i < A.size(); ++i) ownR->set(i, V3{}), sureP->data()[i] = 0.0f;
      std::vector<float> msum(A.size(), 0.0f);
      for (int k = 0; k < 3; ++k) {
        if (part[k] == nullptr) continue;
        auto wk = Plane::allocate(lw, lh);
        if (!wk) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
        for (size_t i = 0; i < A.size(); ++i) wk->data()[i] = wi->data()[i] * smoothstep(0.5f, 0.9f, part[k]->data()[i]);
        auto ref = weightedPlate(gam, *wk, sigma[k], grey(0.3f), token);
        auto dens = blurred(*wk, sigma[k], token);
        if (!ref || !dens) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
        for (size_t i = 0; i < A.size(); ++i) {
          const float m = smoothstep(0.2f, 0.7f, part[k]->data()[i]);
          if (m <= 0.0f) continue;
          ownR->set(i, ownR->at(i) + ref->at(i) * m);
          sureP->data()[i] += m * smoothstep(0.04f, 0.15f, dens->data()[i]);
          msum[i] += m;
        }
      }
      for (size_t i = 0; i < A.size(); ++i) {
        if (msum[i] > 1e-3f) {
          ownR->set(i, ownR->at(i) * (1.0f / msum[i]));
          sureP->data()[i] = sureP->data()[i] / msum[i] * smoothstep(0.2f, 0.6f, msum[i]);
        }
      }
      // Edge pixels the parts map just misses take the nearest part's
      // colour (the parts map is coarse); the trust spreads out with it.
      auto sureW = blurred(*sureP, std::max(1.0f, 0.01f * shortSide), token);
      auto ownW = weightedPlate(*ownR, *sureP, std::max(1.0f, 0.01f * shortSide), grey(0.3f), token);
      if (!sureW || !ownW) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
      // Jewellery and other accessories are their own colour (a gold brooch
      // is not the dress beside it): left alone.
      for (size_t i = 0; i < A.size(); ++i) {
        sureW->data()[i] = std::max(sureP->data()[i], std::min(1.0f, 2.0f * sureW->data()[i]));
        sureW->data()[i] *= 1.0f - smoothstep(0.1f, 0.4f, accessory->data()[i]);
      }
      spillOwn = std::move(ownW).value(), spillSure = std::move(sureW).value();
      spill = true;
    }
  }

  // Full resolution: alpha from the coefficients, colours corrected.
  const float inv = 1.0f / static_cast<float>(f);
  std::vector<double> rowA(static_cast<size_t>(H), 0.0), rowE(static_cast<size_t>(H), 0.0), rowHair(static_cast<size_t>(H), 0.0);
  std::vector<int32_t> rowMin(static_cast<size_t>(H), W), rowMax(static_cast<size_t>(H), -1);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      const uint8_t* src = image.row(y);
      uint8_t* dst = out.row(y);
      const float ly = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      double sa = 0, se = 0, sh = 0;
      int32_t mn = W, mx = -1;
      for (int32_t x = 0; x < W; ++x) {
        const uint8_t* q = src + x * 4;
        uint8_t* d = dst + x * 4;
        const float lx = (static_cast<float>(x) + 0.5f) * inv - 0.5f;
        const float R = q[o.r] / 255.0f, G = q[o.g] / 255.0f, B = q[o.b] / 255.0f;
        float a = std::clamp(mA[0].sample(lx, ly) * R + mA[1].sample(lx, ly) * G + mA[2].sample(lx, ly) * B + mB.sample(lx, ly), 0.0f, 1.0f);
        float twoW = 0.0f;
        V3 hf, hb;
        if (twoColour) {
          const float dd0 = hairD.sample(lx, ly);
          if (dd0 > 0.01f) {
            hf = hairF.sample(lx, ly), hb = hairB.sample(lx, ly);
            float conf = 0.0f;
            const float ap = twoColourAlpha(V3{R, G, B}, hf, hb, conf);
            if (conf > 0.0f) {
              twoW = dd0 * conf;
              a += (ap - a) * twoW;
            }
          }
        }
        if (twoColour) {
          const float m = modelMask.sample(lx, ly);
          a = m + (a - m) * colourSays.sample(lx, ly);
        }
        // Solid inside, clear outside: a person at 97% is see-through, and
        // 2% of the old room is a veil.
        a = std::clamp((a - 0.02f) / 0.94f, 0.0f, 1.0f);
        if (sp.any) {
          a = std::max(a, smoothstep(0.5f, 1.0f, sp.keep.sample(lx, ly)));
          a = std::min(a, 1.0f - smoothstep(0.5f, 1.0f, sp.remove.sample(lx, ly)));
        }
        if (softness > 0.0f) a += (std::clamp(soft.sample(lx, ly), 0.0f, 1.0f) - a) * (0.85f * softness);
        if (shift > 0.0f) {
          const float t = 0.5f + 0.4f * shift;
          a = std::min(a, smoothstep(t - 0.12f, t + 0.12f, shifted.sample(lx, ly)));
        } else if (shift < 0.0f) {
          const float t = 0.5f + 0.4f * shift;
          a = std::max(a, smoothstep(t - 0.12f, t + 0.12f, shifted.sample(lx, ly)));
        }
        V3 col{R, G, B};
        if (clean && a < 0.995f) {
          const V3 fh = Fs.sample(lx, ly), bh = Bs.sample(lx, ly);
          V3 fc = fh + (col - fh * a - bh * (1.0f - a)) * a;
          fc = V3{std::clamp(fc.r, 0.0f, 1.0f), std::clamp(fc.g, 0.0f, 1.0f), std::clamp(fc.b, 0.0f, 1.0f)};
          // Deep inside the person the photo's own pixel stands.
          col = mix(fc, col, smoothstep(0.93f, 0.995f, a));
        }
        if (twoW > 0.0f && a < 0.995f) {
          // Unmixed: the strand's own colour, the old background taken out.
          const V3 I{R, G, B};
          V3 un = (I - hb * (1.0f - a)) * (1.0f / std::max(a, 0.05f));
          un = V3{std::clamp(un.r, 0.0f, 1.0f), std::clamp(un.g, 0.0f, 1.0f), std::clamp(un.b, 0.0f, 1.0f)};
          un = mix(hf, un, smoothstep(0.15f, 0.6f, a));
          col = mix(col, mix(un, I, smoothstep(0.93f, 0.995f, a)), twoW);
        }
        if (spill && a > 0.0f) {
          const float band = rim.sample(lx, ly) * std::clamp(spillSure.sample(lx, ly), 0.0f, 1.0f);
          if (band > 0.01f) col = despill(col, spillOwn.sample(lx, ly), Bw.sample(lx, ly), band);
        }
        d[o.r] = toByte(col.r), d[o.g] = toByte(col.g), d[o.b] = toByte(col.b), d[3] = toByte(a);
        sa += a;
        if (a > 0.02f && a < 0.98f) {
          se += 1.0;
          if (haveHair && hairZone.sample(lx, ly) > 0.5f) sh += 1.0;
        }
        if (a > 0.5f) mn = std::min(mn, x), mx = std::max(mx, x);
      }
      rowA[static_cast<size_t>(y)] = sa, rowE[static_cast<size_t>(y)] = se, rowHair[static_cast<size_t>(y)] = sh;
      rowMin[static_cast<size_t>(y)] = mn, rowMax[static_cast<size_t>(y)] = mx;
    }
  });
  if (!done) return cancelledError();
  if (report != nullptr) {
    double ta = 0, te = 0, th = 0;
    int32_t left = W, right = -1, top = H, bottom = -1;
    for (int32_t y = 0; y < H; ++y) {
      const auto i = static_cast<size_t>(y);
      ta += rowA[i], te += rowE[i], th += rowHair[i];
      if (rowMax[i] >= 0) {
        left = std::min(left, rowMin[i]), right = std::max(right, rowMax[i]);
        top = std::min(top, y), bottom = std::max(bottom, y);
      }
    }
    const double n = static_cast<double>(W) * H;
    report->coverage = static_cast<float>(ta / n);
    report->edge = static_cast<float>(te / n);
    report->hairEdge = te > 0 ? static_cast<float>(th / te) : 0.0f;
    if (right >= 0) {
      report->left = static_cast<float>(left) / static_cast<float>(W);
      report->right = static_cast<float>(right + 1) / static_cast<float>(W);
      report->top = static_cast<float>(top) / static_cast<float>(H);
      report->bottom = static_cast<float>(bottom + 1) / static_cast<float>(H);
      // Running off an edge: a real stretch of the edge is person.
      auto edgeRun = [&](bool horizontal, int32_t at) {
        int32_t on = 0;
        const int32_t len = horizontal ? W : H;
        for (int32_t k = 0; k < len; ++k) {
          const uint8_t* px = horizontal ? out.row(at) + k * 4 : out.row(k) + at * 4;
          if (px[3] > 127) ++on;
        }
        return static_cast<float>(on) / static_cast<float>(len) > 0.04f;
      };
      report->cutTop = edgeRun(true, 0);
      report->cutBottom = edgeRun(true, H - 1);
      report->cutLeft = edgeRun(false, 0);
      report->cutRight = edgeRun(false, W - 1);
    }
    if (zones != nullptr) faceBox(*zones, out, *report), outfitColours(*zones, out, *report);
  }
  return ok();
}

}  // namespace photoplus::studio
