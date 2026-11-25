// Product photography (Phase 12): see product.hpp.
//
// The cut-out works on a small copy (at most 640 px) for everything that
// needs the whole picture (the backdrop's light, separate objects, holes)
// and on the full photo only along the edge, where the backdrop model
// decides each pixel and frees its colour of the old backdrop.
#include "photoplus/product.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/plane.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::product {

namespace {
using portrait::detail::hashNoise;
using portrait::detail::offsets;
using portrait::detail::rgbToYcc;
using portrait::detail::smoothstep;
using portrait::detail::toGamma;
using portrait::detail::toLinear;

inline float c01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline bool colour(const ImageBuffer& img) { return img.format() == PixelFormat::RGBA8 || img.format() == PixelFormat::BGRA8; }
inline bool finite(std::initializer_list<float> v) {
  return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

struct Rgbf {
  float r = 0, g = 0, b = 0;
};

// How far a colour is from the backdrop's at that place. Darker with the
// same colour is what a shadow on the backdrop looks like: it counts for
// little when shadows are left out.
float distance(Rgbf p, Rgbf bg, bool shadowsOut) {
  if (shadowsOut) {
    const float yp = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
    const float yb = std::max(1e-3f, 0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b);
    const float s = yp / yb;
    if (s < 1.0f) {
      // The backdrop in shadow, and how far the colour is from that.
      const float dr = p.r - bg.r * s, dg = p.g - bg.g * s, db = p.b - bg.b * s;
      const float off = std::sqrt(dr * dr + dg * dg + db * db);
      // Shadows on paper rarely take more than half its light; darker
      // than that is the object itself (a black product on white).
      const float dark = s > 0.5f ? 0.06f * (1.0f - s) : 0.03f + 0.6f * (0.5f - s);
      return std::max(2.0f * off, dark);
    }
  }
  float y0, cb0, cr0, y1, cb1, cr1;
  rgbToYcc(p.r, p.g, p.b, y0, cb0, cr0);
  rgbToYcc(bg.r, bg.g, bg.b, y1, cb1, cr1);
  const float dy = y0 - y1, dc = std::hypot(cb0 - cb1, cr0 - cr1);
  return std::sqrt(dy * dy + 4.0f * dc * dc);
}

Result<Plane> blurred(const Plane& src, float sigma, const CancellationToken* token) {
  auto out = Plane::allocate(src.width(), src.height());
  if (!out) return out;
  if (Error e = gaussianBlur(src, *out, sigma, token); e.status != PP_OK) return std::unexpected(e);
  return out;
}

// 4-connected components of `on`; labels from 1, 0 for off.
int32_t label(const std::vector<uint8_t>& on, int32_t w, int32_t h, std::vector<int32_t>& lab, std::vector<int32_t>& area) {
  lab.assign(on.size(), 0);
  area.assign(1, 0);
  std::vector<int32_t> stack;
  int32_t n = 0;
  for (int32_t i = 0; i < w * h; ++i) {
    if (!on[static_cast<size_t>(i)] || lab[static_cast<size_t>(i)]) continue;
    ++n;
    area.push_back(0);
    stack.assign(1, i);
    lab[static_cast<size_t>(i)] = n;
    while (!stack.empty()) {
      const int32_t j = stack.back();
      stack.pop_back();
      ++area[static_cast<size_t>(n)];
      const int32_t x = j % w, y = j / w;
      const int32_t nb[4] = {x > 0 ? j - 1 : -1, x + 1 < w ? j + 1 : -1, y > 0 ? j - w : -1, y + 1 < h ? j + w : -1};
      for (int32_t k : nb)
        if (k >= 0 && on[static_cast<size_t>(k)] && !lab[static_cast<size_t>(k)]) {
          lab[static_cast<size_t>(k)] = n;
          stack.push_back(k);
        }
    }
  }
  return n;
}

// A least-squares plane c = a + b x + c y per channel through the
// samples, reweighted so the object (where it touches the border) counts
// for little.
struct PlaneFit {
  float k[3][3]{};
  Rgbf at(float x, float y) const { return {k[0][0] + k[0][1] * x + k[0][2] * y, k[1][0] + k[1][1] * x + k[1][2] * y, k[2][0] + k[2][1] * x + k[2][2] * y}; }
};
struct Sample {
  float x, y;
  Rgbf c;
};
PlaneFit fitPlane(const std::vector<Sample>& s) {
  PlaneFit fit;
  std::vector<float> w(s.size(), 1.0f);
  for (int it = 0; it < 4; ++it) {
    for (int ch = 0; ch < 3; ++ch) {
      double m[3][4] = {};
      for (size_t i = 0; i < s.size(); ++i) {
        const double v = ch == 0 ? s[i].c.r : ch == 1 ? s[i].c.g : s[i].c.b;
        const double row[3] = {1.0, s[i].x, s[i].y};
        for (int a = 0; a < 3; ++a) {
          for (int b = 0; b < 3; ++b) m[a][b] += w[i] * row[a] * row[b];
          m[a][3] += w[i] * row[a] * v;
        }
      }
      for (int a = 0; a < 3; ++a) m[a][a] += 1e-6;
      // Gauss-Jordan on the 3x3 system.
      for (int c = 0; c < 3; ++c) {
        int piv = c;
        for (int r = c + 1; r < 3; ++r)
          if (std::fabs(m[r][c]) > std::fabs(m[piv][c])) piv = r;
        for (int k = 0; k < 4; ++k) std::swap(m[c][k], m[piv][k]);
        const double d = std::fabs(m[c][c]) > 1e-12 ? m[c][c] : 1e-12;
        for (int k = 0; k < 4; ++k) m[c][k] /= d;
        for (int r = 0; r < 3; ++r)
          if (r != c) {
            const double f = m[r][c];
            for (int k = 0; k < 4; ++k) m[r][k] -= f * m[c][k];
          }
      }
      for (int a = 0; a < 3; ++a) fit.k[ch][a] = static_cast<float>(m[a][3]);
    }
    for (size_t i = 0; i < s.size(); ++i) {
      const Rgbf b = fit.at(s[i].x, s[i].y);
      const float d = std::fabs(s[i].c.r - b.r) + std::fabs(s[i].c.g - b.g) + std::fabs(s[i].c.b - b.b);
      w[i] = 1.0f / (1.0f + (d / 0.06f) * (d / 0.06f));
    }
  }
  return fit;
}

float median(std::vector<float> v) {
  if (v.empty()) return 0.0f;
  const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
  std::nth_element(v.begin(), mid, v.end());
  return *mid;
}

void paint(Plane& a, const studio::Stroke& s, int32_t w, int32_t h, const Plane* key) {
  const float S = static_cast<float>(std::min(w, h));
  const float r = std::max(1.0f, s.radius * S);
  for (const auto& pt : s.points) {
    const float cx = pt[0] * static_cast<float>(w), cy = pt[1] * static_cast<float>(h);
    for (int32_t y = std::max(0, static_cast<int32_t>(cy - r - 1)); y <= std::min(h - 1, static_cast<int32_t>(cy + r + 1)); ++y)
      for (int32_t x = std::max(0, static_cast<int32_t>(cx - r - 1)); x <= std::min(w - 1, static_cast<int32_t>(cx + r + 1)); ++x) {
        const float d = std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy);
        const float t = c01(r + 0.5f - d);  // 1 inside, a 1 px soft rim
        if (t <= 0.0f) continue;
        float& v = a.row(y)[x];
        if (s.brush == studio::Brush::Keep) v = std::max(v, t);
        else if (s.brush == studio::Brush::Remove) v = std::min(v, 1.0f - t);
        else if (key != nullptr) v += (key->row(y)[x] - v) * t;
      }
  }
}
}  // namespace

Error cutout(const ImageBuffer& photo, const ImageBuffer* hint, const std::vector<studio::Stroke>& strokes, const MatteParams& params,
             ImageBuffer& out, const CancellationToken* token, MatteReport* report) {
  ScopedTimer timer("product.cutout");
  if (!colour(photo)) return makeError(PP_ERROR_INVALID_ARGUMENT, "The photo must be RGBA");
  if (out.width() != photo.width() || out.height() != photo.height() || out.format() != photo.format())
    return makeError(PP_ERROR_INVALID_ARGUMENT, "The cut-out must match the photo");
  if (hint != nullptr && (hint->format() != PixelFormat::Gray8 || hint->width() < 2 || hint->height() < 2))
    return makeError(PP_ERROR_INVALID_ARGUMENT, "The object mask must be a grey image");
  if (!finite({params.tolerance, params.softness, params.shift})) return makeError(PP_ERROR_INVALID_ARGUMENT, "Bad cut-out settings");
  const float tolerance = c01(params.tolerance), softness = c01(params.softness), shift = std::clamp(params.shift, -1.0f, 1.0f);
  const int32_t W = photo.width(), H = photo.height();

  // ---- The small copy.
  const float scale = std::min(1.0f, 640.0f / static_cast<float>(std::max(W, H)));
  const int32_t w = std::max(2, static_cast<int32_t>(std::lround(static_cast<float>(W) * scale)));
  const int32_t h = std::max(2, static_cast<int32_t>(std::lround(static_cast<float>(H) * scale)));
  auto smallImg = ops::resize(photo, w, h, ops::ResizeFilter::Area, token);
  if (!smallImg) return smallImg.error();
  const auto po = offsets(photo);
  const size_t N = static_cast<size_t>(w) * static_cast<size_t>(h);
  std::vector<Rgbf> I(N);
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* row = smallImg->row(y);
    for (int32_t x = 0; x < w; ++x) {
      const uint8_t* p = row + x * 4;
      I[static_cast<size_t>(y) * w + x] = {p[po.r] / 255.0f, p[po.g] / 255.0f, p[po.b] / 255.0f};
    }
  }
  const float S = static_cast<float>(std::min(w, h));

  // The hint at the small size.
  Result<Plane> hs = Plane::allocate(1, 1);
  if (hint != nullptr) {
    hs = Plane::allocate(w, h);
    if (!hs) return hs.error();
    const float sx = static_cast<float>(hint->width()) / static_cast<float>(w), sy = static_cast<float>(hint->height()) / static_cast<float>(h);
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const float fx = std::clamp((static_cast<float>(x) + 0.5f) * sx - 0.5f, 0.0f, static_cast<float>(hint->width() - 1));
        const float fy = std::clamp((static_cast<float>(y) + 0.5f) * sy - 0.5f, 0.0f, static_cast<float>(hint->height() - 1));
        const int32_t x0 = static_cast<int32_t>(fx), y0 = static_cast<int32_t>(fy);
        const int32_t x1 = std::min(x0 + 1, hint->width() - 1), y1 = std::min(y0 + 1, hint->height() - 1);
        const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
        const float v = (hint->row(y0)[x0] * (1 - tx) + hint->row(y0)[x1] * tx) * (1 - ty) + (hint->row(y1)[x0] * (1 - tx) + hint->row(y1)[x1] * tx) * ty;
        hs->row(y)[x] = v / 255.0f;
      }
  }
  Result<Plane> hintNear = Plane::allocate(1, 1);
  if (hint != nullptr) {
    hintNear = blurred(*hs, std::max(1.0f, 0.015f * S), token);
    if (!hintNear) return hintNear.error();
  }

  // ---- The backdrop: a plane through the border, then the photo's own
  // backdrop colours spread under the object (its light fall-off included).
  const int32_t band = std::max(2, static_cast<int32_t>(0.04f * S));
  std::vector<Sample> border;
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      if (x >= band && x < w - band && y >= band && y < h - band) continue;
      if ((x + y) % 2) continue;
      border.push_back({static_cast<float>(x) / static_cast<float>(w), static_cast<float>(y) / static_cast<float>(h), I[static_cast<size_t>(y) * w + x]});
    }
  const PlaneFit fit = fitPlane(border);
  std::vector<float> borderDist;
  borderDist.reserve(border.size());
  for (const auto& s : border) borderDist.push_back(distance(s.c, fit.at(s.x, s.y), false));
  const float borderSpread = median(borderDist);
  const float wide = std::max(0.08f, 4.0f * borderSpread);
  int32_t borderBg = 0;
  std::vector<float> bgR, bgG, bgB;
  for (size_t i = 0; i < border.size(); ++i)
    if (borderDist[i] < wide) {
      ++borderBg;
      bgR.push_back(border[i].c.r), bgG.push_back(border[i].c.g), bgB.push_back(border[i].c.b);
    }
  const float borderShare = border.empty() ? 0.0f : static_cast<float>(borderBg) / static_cast<float>(border.size());

  auto wr = Plane::allocate(w, h), wg = Plane::allocate(w, h), wb = Plane::allocate(w, h), ww = Plane::allocate(w, h);
  for (auto* p : {&wr, &wg, &wb, &ww})
    if (!*p) return p->error();
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const Rgbf b0 = fit.at(static_cast<float>(x) / static_cast<float>(w), static_cast<float>(y) / static_cast<float>(h));
      // Only the paper as lit counts (not the object's shadow on it), so
      // the backdrop under a shadow is the unshadowed paper.
      float conf = distance(I[i], b0, false) < wide ? 1.0f : 0.0f;
      if (hint != nullptr && hintNear->row(y)[x] > 0.25f) conf = 0.0f;
      wr->row(y)[x] = I[i].r * conf, wg->row(y)[x] = I[i].g * conf, wb->row(y)[x] = I[i].b * conf, ww->row(y)[x] = conf;
    }
  auto Br = blurred(*wr, 0.06f * S, token), Bg = blurred(*wg, 0.06f * S, token), Bb = blurred(*wb, 0.06f * S, token), Bw = blurred(*ww, 0.06f * S, token);
  auto Fr = blurred(*wr, 0.25f * S, token), Fg = blurred(*wg, 0.25f * S, token), Fb = blurred(*wb, 0.25f * S, token), Fw = blurred(*ww, 0.25f * S, token);
  for (auto* p : {&Br, &Bg, &Bb, &Bw, &Fr, &Fg, &Fb, &Fw})
    if (!*p) return p->error();
  std::vector<Rgbf> B(N);
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const float n = Bw->row(y)[x], f = Fw->row(y)[x];
      Rgbf v = fit.at(static_cast<float>(x) / static_cast<float>(w), static_cast<float>(y) / static_cast<float>(h));
      if (f > 0.01f) v = {Fr->row(y)[x] / f, Fg->row(y)[x] / f, Fb->row(y)[x] / f};
      if (n > 0.02f) {
        const float t = smoothstep(0.02f, 0.15f, n);
        const Rgbf near{Br->row(y)[x] / n, Bg->row(y)[x] / n, Bb->row(y)[x] / n};
        v = {v.r + (near.r - v.r) * t, v.g + (near.g - v.g) * t, v.b + (near.b - v.b) * t};
      }
      B[i] = v;
    }
  std::vector<float> res;
  for (size_t i = 0; i < N; i += 3)
    if (ww->data()[i] > 0.5f) res.push_back(distance(I[i], B[i], false));
  const float spread = median(res);
  const bool plain = spread < 0.03f && borderShare > 0.6f;

  // ---- What differs from the backdrop.
  const float t0 = std::max(0.035f, 2.5f * spread + 0.02f) * (1.6f - 1.2f * tolerance), t1 = t0 * 2.2f;
  auto key = Plane::allocate(w, h), a = Plane::allocate(w, h);
  if (!key || !a) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to cut out");
  for (size_t i = 0; i < N; ++i) key->data()[i] = smoothstep(t0, t1, distance(I[i], B[i], params.shadowsOut));
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      float v = key->data()[i];
      if (hint != nullptr) v = plain ? v * smoothstep(0.05f, 0.3f, hintNear->row(y)[x]) : smoothstep(0.3f, 0.7f, hs->row(y)[x]);
      a->data()[i] = v;
    }

  // ---- Separate objects: specks go, holes in an object are filled when
  // they are part of it (the model says so, or they are small).
  std::vector<uint8_t> on(N);
  for (size_t i = 0; i < N; ++i) on[i] = a->data()[i] > 0.5f;
  std::vector<int32_t> lab, area;
  const int32_t n = label(on, w, h, lab, area);
  const int32_t largest = n > 0 ? *std::max_element(area.begin() + 1, area.end()) : 0;
  const int32_t minArea = std::max(static_cast<int32_t>(0.0004f * static_cast<float>(N)), largest / 20);
  std::vector<uint8_t> keep(static_cast<size_t>(n) + 1, 0);
  int32_t objects = 0;
  for (int32_t k = 1; k <= n; ++k)
    if (area[static_cast<size_t>(k)] >= minArea) keep[static_cast<size_t>(k)] = 1, ++objects;
  {
    auto kept = Plane::allocate(w, h);
    if (!kept) return kept.error();
    for (size_t i = 0; i < N; ++i) kept->data()[i] = lab[i] && keep[static_cast<size_t>(lab[i])] ? 1.0f : 0.0f;
    auto near = blurred(*kept, 1.5f, token);
    if (!near) return near.error();
    for (size_t i = 0; i < N; ++i) {
      if (on[i]) a->data()[i] *= kept->data()[i];
      else if (near->data()[i] < 0.02f) a->data()[i] = 0.0f;
      on[i] = a->data()[i] > 0.5f;
    }
  }
  {
    std::vector<uint8_t> off(N);
    for (size_t i = 0; i < N; ++i) off[i] = !on[i];
    std::vector<int32_t> hl, ha;
    const int32_t hn = label(off, w, h, hl, ha);
    std::vector<uint8_t> outside(static_cast<size_t>(hn) + 1, 0);
    std::vector<double> hintSum(static_cast<size_t>(hn) + 1, 0.0);
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        if (!hl[i]) continue;
        if (x == 0 || y == 0 || x == w - 1 || y == h - 1) outside[static_cast<size_t>(hl[i])] = 1;
        if (hint != nullptr) hintSum[static_cast<size_t>(hl[i])] += hs->data()[i];
      }
    for (size_t i = 0; i < N; ++i) {
      const int32_t k = hl[i];
      if (!k || outside[static_cast<size_t>(k)]) continue;
      const float ar = static_cast<float>(ha[static_cast<size_t>(k)]);
      // Without a model, a white label on a white product looks like the
      // paper: every enclosed area is kept (a missing piece of the product
      // is worse than a filled handle, which the remove brush opens).
      const bool part = hint != nullptr ? hintSum[static_cast<size_t>(k)] / ar > 0.5 : true;
      if (part) a->data()[i] = 1.0f;
    }
  }
  for (const auto& s : strokes) paint(*a, s, w, h, &*key);

  // ---- Edge: shifted and softened.
  {
    const float sigma = 0.7f + (std::fabs(shift) * 0.012f + softness * 0.006f) * S;
    auto sm = blurred(*a, sigma, token);
    if (!sm) return sm.error();
    const float c = 0.5f + 0.3f * shift, wd = 0.08f + 0.35f * softness;
    for (size_t i = 0; i < N; ++i) a->data()[i] = smoothstep(c - wd, c + wd, sm->data()[i]);
  }

  // ---- Report.
  if (report != nullptr) {
    *report = MatteReport{};
    double sum = 0;
    int32_t x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const float v = a->row(y)[x];
        sum += v;
        if (v > 0.5f) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
      }
    report->coverage = static_cast<float>(sum / static_cast<double>(N));
    if (x1 >= 0) {
      report->left = static_cast<float>(x0) / static_cast<float>(w), report->right = static_cast<float>(x1 + 1) / static_cast<float>(w);
      report->top = static_cast<float>(y0) / static_cast<float>(h), report->bottom = static_cast<float>(y1 + 1) / static_cast<float>(h);
    }
    report->backdrop = {median(bgR) * 255.0f, median(bgG) * 255.0f, median(bgB) * 255.0f};
    report->spread = spread;
    report->plain = plain;
    report->objects = objects;
  }

  // ---- Full size: along the edge each pixel is a mix of the object's own
  // colour just inside and the backdrop's: its alpha is where it lies
  // between the two, and its colour is freed of the backdrop.
  auto Pr = Plane::allocate(w, h), Pg = Plane::allocate(w, h), Pb = Plane::allocate(w, h);
  auto Or = Plane::allocate(w, h), Og = Plane::allocate(w, h), Ob = Plane::allocate(w, h), Ow = Plane::allocate(w, h);
  for (auto* q : {&Pr, &Pg, &Pb, &Or, &Og, &Ob, &Ow})
    if (!*q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to cut out");
  for (size_t i = 0; i < N; ++i) Pr->data()[i] = B[i].r, Pg->data()[i] = B[i].g, Pb->data()[i] = B[i].b;
  auto edgeBand = blurred(*a, 1.2f, token);
  if (!edgeBand) return edgeBand.error();
  for (size_t i = 0; i < N; ++i) {
    const float inner = a->data()[i] > 0.99f && edgeBand->data()[i] > 0.97f ? 1.0f : 0.0f;
    Or->data()[i] = I[i].r * inner, Og->data()[i] = I[i].g * inner, Ob->data()[i] = I[i].b * inner, Ow->data()[i] = inner;
  }
  const float objSigma = std::max(1.5f, 0.012f * S);
  auto Qr = blurred(*Or, objSigma, token), Qg = blurred(*Og, objSigma, token), Qb = blurred(*Ob, objSigma, token), Qw = blurred(*Ow, objSigma, token);
  for (auto* q : {&Qr, &Qg, &Qb, &Qw})
    if (!*q) return q->error();
  const auto oo = offsets(out);
  const float fx = static_cast<float>(w) / static_cast<float>(W), fy = static_cast<float>(h) / static_cast<float>(H);
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      const uint8_t* src = photo.row(y);
      uint8_t* dst = out.row(y);
      const float sy = (static_cast<float>(y) + 0.5f) * fy - 0.5f;
      for (int32_t x = 0; x < W; ++x) {
        const float sx = (static_cast<float>(x) + 0.5f) * fx - 0.5f;
        const uint8_t* p = src + x * 4;
        uint8_t* q = dst + x * 4;
        const Rgbf c{p[po.r] / 255.0f, p[po.g] / 255.0f, p[po.b] / 255.0f};
        float v = a->sample(sx, sy);
        Rgbf f = c;
        const float bandV = edgeBand->sample(sx, sy);
        if (plain && bandV > 0.005f && bandV < 0.995f) {
          const Rgbf bg{Pr->sample(sx, sy), Pg->sample(sx, sy), Pb->sample(sx, sy)};
          const float fw = Qw->sample(sx, sy);
          if (fw > 0.02f) {
            const Rgbf obj{Qr->sample(sx, sy) / fw, Qg->sample(sx, sy) / fw, Qb->sample(sx, sy) / fw};
            const float dr = obj.r - bg.r, dg = obj.g - bg.g, db = obj.b - bg.b;
            const float len2 = dr * dr + dg * dg + db * db;
            if (len2 > 0.01f) {
              // Where the pixel lies from the backdrop (0) to the object (1).
              const float t = ((c.r - bg.r) * dr + (c.g - bg.g) * dg + (c.b - bg.b) * db) / len2;
              const float trust = smoothstep(0.01f, 0.04f, len2);
              v += (c01(t) - v) * trust;
            }
          }
          if (v > 0.02f && v < 0.98f) {
            const float inv = 1.0f / v;
            f = {c01((c.r - (1 - v) * bg.r) * inv), c01((c.g - (1 - v) * bg.g) * inv), c01((c.b - (1 - v) * bg.b) * inv)};
          }
        }
        const uint8_t al = static_cast<uint8_t>(std::lround(c01(v) * 255.0f));
        if (al == 0) {
          q[oo.r] = q[oo.g] = q[oo.b] = 0, q[3] = 0;
          continue;
        }
        q[oo.r] = static_cast<uint8_t>(std::lround(f.r * 255.0f));
        q[oo.g] = static_cast<uint8_t>(std::lround(f.g * 255.0f));
        q[oo.b] = static_cast<uint8_t>(std::lround(f.b * 255.0f));
        q[3] = al;
      }
    }
  });
  return done ? Error{PP_OK, {}} : cancelledError();
}

// ---------------------------------------------------------------- render

namespace {

std::array<float, 3> lin(const std::array<uint8_t, 3>& c) { return {toLinear(c[0] / 255.0f), toLinear(c[1] / 255.0f), toLinear(c[2] / 255.0f)}; }

// The backdrop in linear light into r, g, b planes (and a for transparent).
Error paintBackdrop(Plane& R, Plane& G, Plane& Bp, Plane& A, const Backdrop& bd, const ImageBuffer* scene, const CancellationToken* token) {
  const int32_t W = R.width(), H = R.height();
  const auto wall = lin(bd.wall), floor = lin(bd.floor);
  const float light = c01(bd.light), soft = c01(bd.softness), horizon = std::clamp(bd.horizon, 0.05f, 0.95f);
  Result<ImageBuffer> fitted = ImageBuffer::allocate(1, 1, PixelFormat::RGBA8);
  if (bd.kind == BackdropKind::Scene) {
    if (scene == nullptr || !colour(*scene)) return makeError(PP_ERROR_INVALID_ARGUMENT, "A scene backdrop needs its photo");
    // Cover: scaled to fill, centred.
    const float s = std::max(static_cast<float>(W) / static_cast<float>(scene->width()), static_cast<float>(H) / static_cast<float>(scene->height()));
    fitted = ops::resize(*scene, std::max(W, static_cast<int32_t>(std::ceil(static_cast<float>(scene->width()) * s))), std::max(H, static_cast<int32_t>(std::ceil(static_cast<float>(scene->height()) * s))),
                         ops::ResizeFilter::Auto, token);
    if (!fitted) return fitted.error();
  }
  const int32_t ox = bd.kind == BackdropKind::Scene ? (fitted->width() - W) / 2 : 0, oy = bd.kind == BackdropKind::Scene ? (fitted->height() - H) / 2 : 0;
  const auto so = bd.kind == BackdropKind::Scene ? offsets(*fitted) : portrait::detail::Rgb{0, 1, 2};
  const float aspect = static_cast<float>(W) / static_cast<float>(H);
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
      for (int32_t x = 0; x < W; ++x) {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
        std::array<float, 3> c{};
        float alpha = 1.0f;
        float t = 0.0f;  // wall (0) .. floor (1)
        switch (bd.kind) {
          case BackdropKind::Transparent: alpha = 0.0f; break;
          case BackdropKind::Solid: t = 0.0f; break;
          case BackdropKind::Sweep: {
            const float bandW = 0.12f + 0.4f * soft;
            t = smoothstep(horizon - bandW, horizon + bandW * 0.5f, v);
            break;
          }
          case BackdropKind::Surface: {
            const float bandW = 0.003f + 0.04f * soft;
            t = smoothstep(horizon - bandW, horizon + bandW, v);
            break;
          }
          case BackdropKind::Radial: {
            const float dx = (u - 0.5f) * aspect, dy = v - 0.45f;
            t = smoothstep(0.0f, 0.75f + 0.35f * soft, std::sqrt(dx * dx + dy * dy));
            break;
          }
          case BackdropKind::Scene: break;
        }
        if (bd.kind == BackdropKind::Scene) {
          const uint8_t* p = fitted->row(y + oy) + (x + ox) * 4;
          c = {toLinear(p[so.r] / 255.0f), toLinear(p[so.g] / 255.0f), toLinear(p[so.b] / 255.0f)};
        } else {
          for (int k = 0; k < 3; ++k) c[static_cast<size_t>(k)] = wall[static_cast<size_t>(k)] + (floor[static_cast<size_t>(k)] - wall[static_cast<size_t>(k)]) * t;
          if (bd.kind == BackdropKind::Surface && v > horizon) {
            // The table darkens a little toward the camera, the wall upward.
            for (auto& k : c) k *= 1.0f - 0.08f * light * smoothstep(horizon, 1.0f, v);
          }
          if (bd.kind != BackdropKind::Transparent && light > 0.0f) {
            // A pool of light behind the object, the corners a little darker.
            const float dx = (u - 0.5f) * aspect, dy = (v - std::min(horizon, 0.55f)) * 1.2f;
            const float r2 = dx * dx + dy * dy;
            const float pool = 1.0f + 0.10f * light * std::exp(-r2 / 0.08f);
            const float corner = 1.0f - 0.18f * light * smoothstep(0.25f, 0.9f, r2);
            for (auto& k : c) k *= pool * corner;
          }
        }
        R.row(y)[x] = c[0], G.row(y)[x] = c[1], Bp.row(y)[x] = c[2], A.row(y)[x] = alpha;
      }
    }
  });
  return done ? Error{PP_OK, {}} : cancelledError();
}

// The object's silhouette squashed down onto its base line: a target row
// `d` px above the base takes the silhouette `d / k` px above it.
Error squash(const Plane& M, Plane& out, float base, float k, float drop) {
  const int32_t W = M.width(), H = M.height();
  for (int32_t y = 0; y < H; ++y) {
    const float ty = static_cast<float>(y) - drop;
    const float sy = base - (base - ty) / k;
    float* o = out.row(y);
    if (sy < -1.0f || sy > static_cast<float>(H)) {
      std::fill(o, o + W, 0.0f);
      continue;
    }
    for (int32_t x = 0; x < W; ++x) o[x] = sy > base + 0.5f ? 0.0f : M.sample(static_cast<float>(x), sy);
  }
  return Error{PP_OK, {}};
}

}  // namespace

Result<ImageBuffer> render(const ImageBuffer& cutout, const ImageBuffer* scene, const Backdrop& backdrop, const Shadow& shadow,
                           const Correction& corr, const Layout& layout, const CancellationToken* token, RenderReport* report) {
  ScopedTimer timer("product.render");
  if (!colour(cutout)) return std::unexpected(makeError(PP_ERROR_INVALID_ARGUMENT, "The cut-out must be RGBA"));
  if (layout.width < 16 || layout.height < 16 || layout.width > 8192 || layout.height > 8192)
    return std::unexpected(makeError(PP_ERROR_INVALID_ARGUMENT, "The frame must be 16..8192 px"));
  if (static_cast<int32_t>(backdrop.kind) < 0 || static_cast<int32_t>(backdrop.kind) > 5)
    return std::unexpected(makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown backdrop"));
  if (!finite({backdrop.horizon, backdrop.softness, backdrop.light, shadow.contact, shadow.soft, shadow.cast, shadow.castAngle, shadow.castLength,
               shadow.castSoftness, shadow.reflection, corr.exposure, corr.contrast, corr.saturation, corr.warmth, corr.tint, corr.reference[0],
               corr.reference[1], corr.reference[2], layout.fill, layout.x, layout.y}))
    return std::unexpected(makeError(PP_ERROR_INVALID_ARGUMENT, "Bad product settings"));
  const int32_t W = layout.width, H = layout.height;

  // ---- The object's box (alpha > 0.5) and what to crop (any alpha).
  int32_t bx0 = cutout.width(), by0 = cutout.height(), bx1 = -1, by1 = -1, cx0 = cutout.width(), cy0 = cutout.height(), cx1 = -1, cy1 = -1;
  for (int32_t y = 0; y < cutout.height(); ++y) {
    const uint8_t* row = cutout.row(y);
    for (int32_t x = 0; x < cutout.width(); ++x) {
      const uint8_t al = row[x * 4 + 3];
      if (al > 4) cx0 = std::min(cx0, x), cx1 = std::max(cx1, x), cy0 = std::min(cy0, y), cy1 = std::max(cy1, y);
      if (al > 127) bx0 = std::min(bx0, x), bx1 = std::max(bx1, x), by0 = std::min(by0, y), by1 = std::max(by1, y);
    }
  }
  if (bx1 < 0) return std::unexpected(makeError(PP_ERROR_INVALID_ARGUMENT, "No object in the cut-out"));
  const float bw = static_cast<float>(bx1 - bx0 + 1), bh = static_cast<float>(by1 - by0 + 1);
  const float fill = std::clamp(layout.fill, 0.05f, 1.0f);
  const float s = fill * std::min(static_cast<float>(W) / bw, static_cast<float>(H) / bh);
  const int32_t cropW = cx1 - cx0 + 1, cropH = cy1 - cy0 + 1;
  const int32_t pw = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(cropW) * s)));
  const int32_t ph = std::max(1, static_cast<int32_t>(std::lround(static_cast<float>(cropH) * s)));
  if (static_cast<int64_t>(pw) * ph > 80'000'000LL) return std::unexpected(makeError(PP_ERROR_INVALID_ARGUMENT, "The object would be too large"));
  // The crop as its own image, then scaled (premultiplied inside resize()).
  auto crop = ImageBuffer::allocate(cropW, cropH, cutout.format());
  if (!crop) return std::unexpected(crop.error());
  for (int32_t y = 0; y < cropH; ++y) std::memcpy(crop->row(y), cutout.row(cy0 + y) + cx0 * 4, static_cast<size_t>(cropW) * 4);
  auto obj = ops::resize(*crop, pw, ph, ops::ResizeFilter::Auto, token);
  if (!obj) return std::unexpected(obj.error());
  // Where the crop's top-left lands.
  const float boxW = bw * s, boxH = bh * s;
  const float boxCx = c01(layout.x) * static_cast<float>(W);
  const float boxTop = layout.ground ? c01(layout.y) * static_cast<float>(H) - boxH : c01(layout.y) * static_cast<float>(H) - boxH * 0.5f;
  const float boxLeft = boxCx - boxW * 0.5f;
  // The crop's offset from the box (mirrored when flipped).
  const float offL = layout.flip ? static_cast<float>(cx1 - bx1) * s : static_cast<float>(bx0 - cx0) * s;
  const int32_t ox = static_cast<int32_t>(std::lround(boxLeft - offL));
  const int32_t oy = static_cast<int32_t>(std::lround(boxTop - static_cast<float>(by0 - cy0) * s));
  const float base = boxTop + boxH;  // the object's base line

  // ---- Colour correction, measured from the old backdrop.
  std::array<float, 3> gain{1.0f, 1.0f, 1.0f};
  const bool hasRef = corr.reference[0] >= 0.0f && corr.reference[1] >= 0.0f && corr.reference[2] >= 0.0f;
  if (hasRef) {
    const std::array<float, 3> ref{std::max(1e-3f, toLinear(corr.reference[0] / 255.0f)), std::max(1e-3f, toLinear(corr.reference[1] / 255.0f)),
                                   std::max(1e-3f, toLinear(corr.reference[2] / 255.0f))};
    const float mean = (ref[0] + ref[1] + ref[2]) / 3.0f;
    if (corr.autoWhite)
      for (int k = 0; k < 3; ++k) gain[static_cast<size_t>(k)] = std::clamp(mean / ref[static_cast<size_t>(k)], 0.75f, 1.33f);
    const float sat = (std::max({ref[0], ref[1], ref[2]}) - std::min({ref[0], ref[1], ref[2]})) / std::max(1e-3f, std::max({ref[0], ref[1], ref[2]}));
    if (corr.autoExposure && sat < 0.25f && mean > 0.12f) {
      const float e = std::clamp(0.88f / mean, 0.85f, 2.2f);  // the backdrop would have been near white
      for (auto& g : gain) g *= e;
    }
  }
  const float ev = std::exp2(std::clamp(corr.exposure, -1.0f, 1.0f));
  const float warmth = std::clamp(corr.warmth, -1.0f, 1.0f), tint = std::clamp(corr.tint, -1.0f, 1.0f);
  gain[0] *= ev * (1.0f + 0.12f * warmth + 0.04f * tint);
  gain[1] *= ev * (1.0f - 0.08f * tint);
  gain[2] *= ev * (1.0f - 0.14f * warmth + 0.04f * tint);
  const float contrast = std::clamp(corr.contrast, -1.0f, 1.0f), saturation = std::clamp(corr.saturation, -1.0f, 1.0f);
  const auto oo = offsets(*obj);
  std::vector<float> L(256);
  for (int i = 0; i < 256; ++i) L[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
  for (int32_t y = 0; y < ph; ++y) {
    uint8_t* row = obj->row(y);
    for (int32_t x = 0; x < pw; ++x) {
      uint8_t* p = row + x * 4;
      if (p[3] == 0) continue;
      float c[3] = {L[p[oo.r]] * gain[0], L[p[oo.g]] * gain[1], L[p[oo.b]] * gain[2]};
      // Highlights roll off instead of clipping.
      const float mx = std::max({c[0], c[1], c[2]});
      if (mx > 0.9f) {
        const float k = (0.9f + 0.1f * (1.0f - std::exp(-(mx - 0.9f) * 5.0f))) / mx;
        for (float& v : c) v *= k;
      }
      float g[3] = {toGamma(c[0]), toGamma(c[1]), toGamma(c[2])};
      if (contrast != 0.0f)
        for (float& v : g) {
          const float sc = v < 0.5f ? 0.5f * std::pow(2.0f * v, 1.0f + contrast * 0.6f) : 1.0f - 0.5f * std::pow(2.0f * (1.0f - v), 1.0f + contrast * 0.6f);
          v = c01(sc);
        }
      if (saturation != 0.0f) {
        const float l = 0.299f * g[0] + 0.587f * g[1] + 0.114f * g[2];
        for (float& v : g) v = c01(l + (v - l) * (1.0f + saturation));
      }
      p[oo.r] = static_cast<uint8_t>(std::lround(c01(g[0]) * 255.0f));
      p[oo.g] = static_cast<uint8_t>(std::lround(c01(g[1]) * 255.0f));
      p[oo.b] = static_cast<uint8_t>(std::lround(c01(g[2]) * 255.0f));
    }
  }

  // ---- The backdrop.
  auto R = Plane::allocate(W, H), G = Plane::allocate(W, H), Bp = Plane::allocate(W, H), A = Plane::allocate(W, H), M = Plane::allocate(W, H);
  for (auto* p : {&R, &G, &Bp, &A, &M})
    if (!*p) return std::unexpected(p->error());
  if (Error e = paintBackdrop(*R, *G, *Bp, *A, backdrop, scene, token); e.status != PP_OK) return std::unexpected(e);

  // The object's alpha in the frame.
  std::fill(M->data(), M->data() + M->size(), 0.0f);
  auto objAt = [&](int32_t fx, int32_t fy) -> const uint8_t* {
    const int32_t x = layout.flip ? pw - 1 - (fx - ox) : fx - ox, y = fy - oy;
    if (x < 0 || y < 0 || x >= pw || y >= ph) return nullptr;
    return obj->row(y) + x * 4;
  };
  for (int32_t y = std::max(0, oy); y < std::min(H, oy + ph); ++y)
    for (int32_t x = std::max(0, ox); x < std::min(W, ox + pw); ++x)
      if (const uint8_t* p = objAt(x, y)) M->row(y)[x] = p[3] / 255.0f;

  // ---- Shadows: darkness 0..1 in D.
  auto D = Plane::allocate(W, H), tmp = Plane::allocate(W, H);
  if (!D || !tmp) return std::unexpected(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for shadows"));
  std::fill(D->data(), D->data() + D->size(), 0.0f);
  auto addLayer = [&](const Plane& layer, float strength) {
    for (size_t i = 0; i < D->size(); ++i) D->data()[i] = 1.0f - (1.0f - D->data()[i]) * (1.0f - c01(layer.data()[i] * strength));
  };
  const float contact = c01(shadow.contact), softSh = c01(shadow.soft), cast = c01(shadow.cast), refl = c01(shadow.reflection);
  if (contact > 0.0f) {
    squash(*M, *tmp, base, 0.035f, 0.01f * boxH);
    auto b = blurred(*tmp, std::max(1.0f, 0.02f * boxW), token);
    if (!b) return std::unexpected(b.error());
    addLayer(*b, 0.85f * contact);
  }
  if (softSh > 0.0f) {
    squash(*M, *tmp, base, 0.12f, 0.03f * boxH);
    auto b = blurred(*tmp, std::max(2.0f, 0.07f * boxW), token);
    if (!b) return std::unexpected(b.error());
    addLayer(*b, 0.55f * softSh);
  }
  if (cast > 0.0f && std::fabs(shadow.castLength) > 0.01f) {
    // The silhouette laid on the floor: away from the light, leaning by
    // castAngle, foreshortened by castLength; softer and fainter with
    // distance from the object.
    const float len = std::clamp(shadow.castLength, -1.5f, 1.5f);
    const float lean = std::tan(std::clamp(shadow.castAngle, -70.0f, 70.0f) * 3.14159265f / 180.0f);
    for (int32_t y = 0; y < H; ++y) {
      float* o = tmp->row(y);
      const float d = len > 0 ? base - static_cast<float>(y) : static_cast<float>(y) - base;  // distance along the floor
      if (d < 0.0f) {
        std::fill(o, o + W, 0.0f);
        continue;
      }
      const float sy = base - d / std::fabs(len);
      const float fade = 1.0f - 0.7f * c01(d / (std::fabs(len) * boxH));
      for (int32_t x = 0; x < W; ++x) o[x] = M->sample(static_cast<float>(x) - lean * d, sy) * fade;
    }
    const float soft = c01(shadow.castSoftness);
    auto near = blurred(*tmp, std::max(1.0f, 0.008f * boxH), token), far = blurred(*tmp, std::max(2.0f, (0.02f + 0.08f * soft) * boxH), token);
    if (!near || !far) return std::unexpected(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for shadows"));
    for (int32_t y = 0; y < H; ++y) {
      const float d = len > 0 ? base - static_cast<float>(y) : static_cast<float>(y) - base;
      const float t = c01(d / (std::fabs(len) * boxH));
      for (int32_t x = 0; x < W; ++x) tmp->row(y)[x] = near->row(y)[x] + (far->row(y)[x] - near->row(y)[x]) * t;
    }
    addLayer(*tmp, 0.6f * cast);
  }
  // The object stands in front of its own shadow.
  for (size_t i = 0; i < D->size(); ++i) D->data()[i] *= 1.0f - M->data()[i];

  // ---- Compose: backdrop darkened by the shadows, the reflection, the object.
  auto out = ImageBuffer::allocate(W, H, cutout.format());
  if (!out) return std::unexpected(out.error());
  const auto po = offsets(*out);
  const float reflLen = 0.45f * boxH;
  const uint32_t seed = backdrop.seed;
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      uint8_t* row = out->row(y);
      for (int32_t x = 0; x < W; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
        const float sh = D->data()[i] * 0.85f;
        float c[3] = {R->data()[i] * (1.0f - sh), G->data()[i] * (1.0f - sh), Bp->data()[i] * (1.0f - sh)};
        float al = A->data()[i];
        if (al < 1.0f) {
          // Transparent: the shadow is soft black over nothing.
          const float sa = sh;
          al = al + sa * (1.0f - al);
          if (al > 0.0f) for (float& v : c) v *= (1.0f - sa) * A->data()[i] / al;
        }
        if (refl > 0.0f && static_cast<float>(y) > base && static_cast<float>(y) < base + reflLen) {
          const float d = static_cast<float>(y) - base;
          const int32_t sy = static_cast<int32_t>(std::lround(base - d - 1.0f));
          if (const uint8_t* p = objAt(x, sy)) {
            const float k = refl * 0.55f * (p[3] / 255.0f) * std::pow(1.0f - d / reflLen, 1.6f);
            c[0] += (L[p[po.r]] - c[0]) * k, c[1] += (L[p[po.g]] - c[1]) * k, c[2] += (L[p[po.b]] - c[2]) * k;
            al = al + k * (1.0f - al);
          }
        }
        float g[3] = {toGamma(c[0]), toGamma(c[1]), toGamma(c[2])};
        if (const uint8_t* p = objAt(x, y)) {
          const float a = p[3] / 255.0f;
          g[0] += (p[po.r] / 255.0f - g[0]) * a, g[1] += (p[po.g] / 255.0f - g[1]) * a, g[2] += (p[po.b] / 255.0f - g[2]) * a;
          al = al + a * (1.0f - al);
        }
        const float n = hashNoise(x, y, seed) * (0.6f / 255.0f);  // dither: no banding in smooth backdrops
        uint8_t* q = row + x * 4;
        q[po.r] = static_cast<uint8_t>(std::lround(c01(g[0] + n) * 255.0f));
        q[po.g] = static_cast<uint8_t>(std::lround(c01(g[1] + n) * 255.0f));
        q[po.b] = static_cast<uint8_t>(std::lround(c01(g[2] + n) * 255.0f));
        q[3] = static_cast<uint8_t>(std::lround(c01(al) * 255.0f));
      }
    }
  });
  if (!done) return std::unexpected(cancelledError());
  if (report != nullptr) {
    report->left = boxLeft / static_cast<float>(W), report->right = (boxLeft + boxW) / static_cast<float>(W);
    report->top = boxTop / static_cast<float>(H), report->bottom = base / static_cast<float>(H);
    report->gain = gain;
  }
  return out;
}

}  // namespace photoplus::product
