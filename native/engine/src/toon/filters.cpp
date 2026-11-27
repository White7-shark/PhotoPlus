#include "filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include "photoplus/parallel.hpp"

namespace photoplus::toon::filters {

using namespace art::core;

namespace {

inline size_t idx(int x, int y, int w) { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); }

}  // namespace

Img boxMean(const Img& src, int r, const CancellationToken* token) {
  const int w = src.w, h = src.h;
  if (r <= 0) return src;
  Img tmp(w, h, 1), out(w, h, 1);
  // Rows: a running sum over the clamped window.
  parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y) {
      const float* row = &src.d[idx(0, y, w)];
      float* o = &tmp.d[idx(0, y, w)];
      double s = 0;
      for (int k = -r; k <= r; ++k) s += row[std::clamp(k, 0, w - 1)];
      for (int x = 0; x < w; ++x) {
        o[x] = static_cast<float>(s);
        s += row[std::min(x + r + 1, w - 1)] - row[std::max(x - r, 0)];
      }
    }
  });
  // Columns, a narrow band of columns at a time (enough bands for every core).
  const float inv = 1.0f / static_cast<float>((2 * r + 1) * (2 * r + 1));
  constexpr int kBand = 8;
  parallelFor((w + kBand - 1) / kBand, token, [&](int32_t b0, int32_t b1) {
    std::vector<double> s(kBand);
    for (int b = b0; b < b1; ++b) {
      const int xa = b * kBand, xb = std::min(w, xa + kBand);
      for (int x = xa; x < xb; ++x) {
        double v = 0;
        for (int k = -r; k <= r; ++k) v += tmp.d[idx(x, std::clamp(k, 0, h - 1), w)];
        s[static_cast<size_t>(x - xa)] = v;
      }
      for (int y = 0; y < h; ++y)
        for (int x = xa; x < xb; ++x) {
          double& v = s[static_cast<size_t>(x - xa)];
          out.d[idx(x, y, w)] = static_cast<float>(v) * inv;
          v += tmp.d[idx(x, std::min(y + r + 1, h - 1), w)] - tmp.d[idx(x, std::max(y - r, 0), w)];
        }
    }
  });
  return out;
}

namespace {

template <typename Fn>
void eachPixel(size_t n, int w, const CancellationToken* token, Fn&& fn) {
  const int rows = static_cast<int>(n / static_cast<size_t>(w));
  parallelFor(rows, token, [&](int32_t y0, int32_t y1) {
    for (size_t i = static_cast<size_t>(y0) * static_cast<size_t>(w); i < static_cast<size_t>(y1) * static_cast<size_t>(w); ++i) fn(i);
  });
}

}  // namespace

Img guided(const Img& I, const Img& p, int r, float eps, const CancellationToken* token) {
  const size_t n = I.d.size();
  Img ii(I.w, I.h, 1), ip(I.w, I.h, 1);
  eachPixel(n, I.w, token, [&](size_t i) { ii.d[i] = I.d[i] * I.d[i], ip.d[i] = I.d[i] * p.d[i]; });
  const Img mI = boxMean(I, r, token), mP = boxMean(p, r, token), mII = boxMean(ii, r, token), mIP = boxMean(ip, r, token);
  Img a(I.w, I.h, 1), b(I.w, I.h, 1);
  eachPixel(n, I.w, token, [&](size_t i) {
    const float var = mII.d[i] - mI.d[i] * mI.d[i], cov = mIP.d[i] - mI.d[i] * mP.d[i];
    a.d[i] = cov / (var + eps);
    b.d[i] = mP.d[i] - a.d[i] * mI.d[i];
  });
  const Img mA = boxMean(a, r, token), mB = boxMean(b, r, token);
  Img q(I.w, I.h, 1);
  eachPixel(n, I.w, token, [&](size_t i) { q.d[i] = mA.d[i] * I.d[i] + mB.d[i]; });
  return q;
}

Img guidedFast(const Img& I, const Img& p, int r, float eps, const CancellationToken* token) {
  if (I.w < 64 || I.h < 64 || r < 2) return guided(I, p, r, eps, token);
  // He and Sun's fast guided filter: the linear coefficients found at half size, applied at full size.
  const Img Is = down2(I), ps = down2(p);
  const size_t n = Is.d.size();
  const int rs = std::max(1, r / 2);
  Img ii(Is.w, Is.h, 1), ip(Is.w, Is.h, 1);
  eachPixel(n, Is.w, token, [&](size_t i) { ii.d[i] = Is.d[i] * Is.d[i], ip.d[i] = Is.d[i] * ps.d[i]; });
  const Img mI = boxMean(Is, rs, token), mP = boxMean(ps, rs, token), mII = boxMean(ii, rs, token), mIP = boxMean(ip, rs, token);
  Img a(Is.w, Is.h, 1), b(Is.w, Is.h, 1);
  eachPixel(n, Is.w, token, [&](size_t i) {
    const float var = mII.d[i] - mI.d[i] * mI.d[i], cov = mIP.d[i] - mI.d[i] * mP.d[i];
    a.d[i] = cov / (var + eps);
    b.d[i] = mP.d[i] - a.d[i] * mI.d[i];
  });
  const Img mA = resize(boxMean(a, rs, token), I.w, I.h), mB = resize(boxMean(b, rs, token), I.w, I.h);
  Img q(I.w, I.h, 1);
  eachPixel(I.d.size(), I.w, token, [&](size_t i) { q.d[i] = mA.d[i] * I.d[i] + mB.d[i]; });
  return q;
}

float noiseSigma(const Img& l) {
  const int w = l.w, h = l.h;
  if (w < 8 || h < 8) return 0;
  // |Laplacian difference| and the local gradient, on a grid; the flattest half decides.
  std::vector<std::pair<float, float>> v;
  v.reserve(static_cast<size_t>(w / 2) * static_cast<size_t>(h / 2));
  const int step = std::max(2, std::min(w, h) / 400);
  for (int y = 1; y < h - 1; y += step)
    for (int x = 1; x < w - 1; x += step) {
      auto at = [&](int xx, int yy) { return l.d[idx(xx, yy, w)]; };
      const float c = at(x, y);
      const float lap = at(x - 1, y - 1) - 2 * at(x, y - 1) + at(x + 1, y - 1) - 2 * at(x - 1, y) + 4 * c - 2 * at(x + 1, y) + at(x - 1, y + 1) -
                        2 * at(x, y + 1) + at(x + 1, y + 1);
      const float g = std::fabs(at(x + 1, y) - at(x - 1, y)) + std::fabs(at(x, y + 1) - at(x, y - 1));
      v.emplace_back(g, std::fabs(lap));
    }
  if (v.empty()) return 0;
  const size_t half = v.size() / 2;
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(half), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
  double s = 0;
  for (size_t i = 0; i < half; ++i) s += v[i].second;
  return static_cast<float>(std::sqrt(kPi / 2) / 6.0 * s / static_cast<double>(std::max<size_t>(1, half)));
}

void clahe(Img& L, int tile, float clip, const CancellationToken* token) {
  const int w = L.w, h = L.h;
  constexpr int kBins = 256;
  const int nx = std::clamp(w / std::max(8, tile), 2, 16), ny = std::clamp(h / std::max(8, tile), 2, 16);
  std::vector<std::array<float, kBins>> lut(static_cast<size_t>(nx * ny));
  auto bin = [](float v) { return std::clamp(static_cast<int>(v * 2.55f), 0, kBins - 1); };
  parallelFor(ny, token, [&](int32_t t0, int32_t t1) {
    for (int ty = t0; ty < t1; ++ty)
      for (int tx = 0; tx < nx; ++tx) {
        const int x0 = tx * w / nx, x1 = (tx + 1) * w / nx, y0 = ty * h / ny, y1 = (ty + 1) * h / ny;
        std::array<float, kBins> hist{};
        for (int y = y0; y < y1; ++y)
          for (int x = x0; x < x1; ++x) hist[static_cast<size_t>(bin(L.d[idx(x, y, w)]))] += 1;
        const float count = static_cast<float>((x1 - x0) * (y1 - y0));
        const float limit = std::max(1.0f, clip * count / kBins);
        float excess = 0;
        for (float& c : hist)
          if (c > limit) excess += c - limit, c = limit;
        const float add = excess / kBins;
        float cum = 0;
        auto& m = lut[static_cast<size_t>(ty * nx + tx)];
        for (int b = 0; b < kBins; ++b) {
          cum += hist[static_cast<size_t>(b)] + add;
          m[static_cast<size_t>(b)] = 100.0f * cum / count;
        }
      }
  });
  parallelFor(h, token, [&](int32_t y0, int32_t y1) {
    for (int y = y0; y < y1; ++y) {
      const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(ny) / static_cast<float>(h) - 0.5f;
      const int ty = std::clamp(static_cast<int>(std::floor(fy)), 0, ny - 1), ty1 = std::min(ty + 1, ny - 1);
      const float wy = std::clamp(fy - static_cast<float>(ty), 0.0f, 1.0f);
      for (int x = 0; x < w; ++x) {
        const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(nx) / static_cast<float>(w) - 0.5f;
        const int tx = std::clamp(static_cast<int>(std::floor(fx)), 0, nx - 1), tx1 = std::min(tx + 1, nx - 1);
        const float wx = std::clamp(fx - static_cast<float>(tx), 0.0f, 1.0f);
        float& v = L.d[idx(x, y, w)];
        const size_t b = static_cast<size_t>(bin(v));
        const float a = lut[static_cast<size_t>(ty * nx + tx)][b], c = lut[static_cast<size_t>(ty * nx + tx1)][b];
        const float d = lut[static_cast<size_t>(ty1 * nx + tx)][b], e = lut[static_cast<size_t>(ty1 * nx + tx1)][b];
        v = mix(mix(a, c, wx), mix(d, e, wx), wy);
      }
    }
  });
}

std::vector<std::array<float, 3>> kmeansLab(const Img& lab, const Img* where, int k, float lWeight, int samples, uint32_t seed) {
  std::vector<std::array<float, 3>> pts;
  const size_t n = static_cast<size_t>(lab.w) * static_cast<size_t>(lab.h);
  size_t eligible = 0;
  for (size_t i = 0; i < n; ++i)
    if (where == nullptr || where->d[i] > 0.5f) ++eligible;
  if (eligible == 0) return {};
  const size_t stride = std::max<size_t>(1, eligible / static_cast<size_t>(std::max(1, samples)));
  size_t seen = 0;
  for (size_t i = 0; i < n; ++i) {
    if (where != nullptr && where->d[i] <= 0.5f) continue;
    if (seen++ % stride != 0) continue;
    pts.push_back({lab.d[i * 3] * lWeight, lab.d[i * 3 + 1], lab.d[i * 3 + 2]});
  }
  k = std::clamp(k, 1, static_cast<int>(pts.size()));
  auto d2 = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
  };
  // k-means++ seeding.
  Rng rng(seed * 2654435761U + 17U);
  std::vector<std::array<float, 3>> c;
  c.push_back(pts[static_cast<size_t>(rng.uni() * static_cast<float>(pts.size())) % pts.size()]);
  std::vector<float> best(pts.size(), 1e30f);
  while (static_cast<int>(c.size()) < k) {
    double total = 0;
    for (size_t i = 0; i < pts.size(); ++i) best[i] = std::min(best[i], d2(pts[i], c.back())), total += best[i];
    if (total <= 0) break;
    double pick = rng.uni() * total;
    size_t chosen = pts.size() - 1;
    for (size_t i = 0; i < pts.size(); ++i) {
      pick -= best[i];
      if (pick <= 0) {
        chosen = i;
        break;
      }
    }
    c.push_back(pts[chosen]);
  }
  // Lloyd iterations.
  std::vector<int> label(pts.size(), 0);
  for (int it = 0; it < 12; ++it) {
    bool moved = false;
    for (size_t i = 0; i < pts.size(); ++i) {
      int bi = 0;
      float bd = 1e30f;
      for (size_t j = 0; j < c.size(); ++j) {
        const float d = d2(pts[i], c[j]);
        if (d < bd) bd = d, bi = static_cast<int>(j);
      }
      if (bi != label[i]) moved = true, label[i] = bi;
    }
    std::vector<std::array<double, 4>> acc(c.size(), {0, 0, 0, 0});
    for (size_t i = 0; i < pts.size(); ++i) {
      auto& a = acc[static_cast<size_t>(label[i])];
      a[0] += pts[i][0], a[1] += pts[i][1], a[2] += pts[i][2], a[3] += 1;
    }
    for (size_t j = 0; j < c.size(); ++j)
      if (acc[j][3] > 0) c[j] = {static_cast<float>(acc[j][0] / acc[j][3]), static_cast<float>(acc[j][1] / acc[j][3]), static_cast<float>(acc[j][2] / acc[j][3])};
    if (!moved && it > 0) break;
  }
  for (auto& cc : c) cc[0] /= lWeight;
  return c;
}

void assignPalette(const Img& lab, const std::vector<std::array<float, 3>>& pal, float lWeight, float soft, Img& out,
                   const CancellationToken* token) {
  out = Img(lab.w, lab.h, 3);
  if (pal.empty()) {
    out = lab;
    return;
  }
  const size_t n = static_cast<size_t>(lab.w) * static_cast<size_t>(lab.h);
  const float lw2 = lWeight * lWeight;
  parallelFor(lab.h, token, [&](int32_t y0, int32_t y1) {
    for (size_t i = static_cast<size_t>(y0) * static_cast<size_t>(lab.w); i < std::min(n, static_cast<size_t>(y1) * static_cast<size_t>(lab.w)); ++i) {
      const float* p = &lab.d[i * 3];
      float d1 = 1e30f, d2 = 1e30f;
      size_t b1 = 0, b2 = 0;
      for (size_t j = 0; j < pal.size(); ++j) {
        const float x = p[0] - pal[j][0], y = p[1] - pal[j][1], z = p[2] - pal[j][2];
        const float d = lw2 * x * x + y * y + z * z;
        if (d < d1) d2 = d1, b2 = b1, d1 = d, b1 = j;
        else if (d < d2) d2 = d, b2 = j;
      }
      // Across a cel boundary: half and half at the boundary, the nearest a few units away.
      const float margin = std::sqrt(d2) - std::sqrt(d1);
      const float t = pal.size() > 1 && soft > 0 ? 0.5f * (1 - smoothstep(0.0f, soft, margin)) : 0.0f;
      for (int k = 0; k < 3; ++k) out.d[i * 3 + static_cast<size_t>(k)] = mix(pal[b1][static_cast<size_t>(k)], pal[b2][static_cast<size_t>(k)], t);
    }
  });
}

void dropSpecks(Img& ink, int minArea) {
  if (minArea <= 1) return;
  const int w = ink.w, h = ink.h;
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  std::vector<int32_t> parent(n, -1);
  auto find = [&](int32_t a) {
    while (parent[static_cast<size_t>(a)] != a) {
      parent[static_cast<size_t>(a)] = parent[static_cast<size_t>(parent[static_cast<size_t>(a)])];
      a = parent[static_cast<size_t>(a)];
    }
    return a;
  };
  auto unite = [&](int32_t a, int32_t b) {
    a = find(a), b = find(b);
    if (a != b) parent[static_cast<size_t>(std::max(a, b))] = std::min(a, b);
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = idx(x, y, w);
      if (ink.d[i] <= 0.35f) continue;
      parent[i] = static_cast<int32_t>(i);
      const int nb[4][2] = {{-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
      for (const auto& o : nb) {
        const int xx = x + o[0], yy = y + o[1];
        if (xx < 0 || yy < 0 || xx >= w) continue;
        const size_t j = idx(xx, yy, w);
        if (parent[j] >= 0) unite(static_cast<int32_t>(i), static_cast<int32_t>(j));
      }
    }
  std::vector<int32_t> area(n, 0);
  for (size_t i = 0; i < n; ++i)
    if (parent[i] >= 0) ++area[static_cast<size_t>(find(static_cast<int32_t>(i)))];
  // Small pieces go, with the faint fringe around them (within 2 px, unless a kept line is as near).
  Img drop(w, h, 1, 0.0f), kept(w, h, 1, 0.0f);
  bool any = false;
  for (size_t i = 0; i < n; ++i) {
    if (parent[i] < 0) continue;
    if (area[static_cast<size_t>(find(static_cast<int32_t>(i)))] < minArea) drop.d[i] = 1, any = true;
    else kept.d[i] = 1;
  }
  if (!any) return;
  auto dilate2 = [&](const Img& src) {
    Img a(w, h, 1, 0.0f), b(w, h, 1, 0.0f);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float v = 0;
        for (int o = -2; o <= 2; ++o) v = std::max(v, src.d[idx(std::clamp(x + o, 0, w - 1), y, w)]);
        a.d[idx(x, y, w)] = v;
      }
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float v = 0;
        for (int o = -2; o <= 2; ++o) v = std::max(v, a.d[idx(x, std::clamp(y + o, 0, h - 1), w)]);
        b.d[idx(x, y, w)] = v;
      }
    return b;
  };
  const Img nearDrop = dilate2(drop), nearKept = dilate2(kept);
  for (size_t i = 0; i < n; ++i)
    if (drop.d[i] > 0 || (parent[i] < 0 && nearDrop.d[i] > 0 && nearKept.d[i] <= 0)) ink.d[i] = 0;
}

std::vector<Pt> smoothCurve(const std::vector<Pt>& p, int steps, bool closed) {
  const int n = static_cast<int>(p.size());
  if (n < 3 || steps <= 1) return p;
  std::vector<Pt> out;
  auto get = [&](int i) {
    if (closed) return p[static_cast<size_t>(((i % n) + n) % n)];
    return p[static_cast<size_t>(std::clamp(i, 0, n - 1))];
  };
  const int segs = closed ? n : n - 1;
  for (int i = 0; i < segs; ++i) {
    const Pt p0 = get(i - 1), p1 = get(i), p2 = get(i + 1), p3 = get(i + 2);
    for (int s = 0; s < steps; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(steps), t2 = t * t, t3 = t2 * t;
      auto cr = [&](float a, float b, float c, float d) { return 0.5f * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3); };
      out.push_back({cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y)});
    }
  }
  if (!closed) out.push_back(p.back());
  return out;
}

bool inside(const std::vector<Pt>& poly, float x, float y) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const Pt& a = poly[i];
    const Pt& b = poly[j];
    if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
  }
  return in;
}

namespace {

float segDist(float px, float py, const Pt& a, const Pt& b, float* tOut) {
  const float vx = b.x - a.x, vy = b.y - a.y;
  const float l2 = vx * vx + vy * vy;
  const float t = l2 > 0 ? std::clamp(((px - a.x) * vx + (py - a.y) * vy) / l2, 0.0f, 1.0f) : 0.0f;
  if (tOut != nullptr) *tOut = t;
  return std::hypot(px - (a.x + t * vx), py - (a.y + t * vy));
}

}  // namespace

void fillPolygon(Img& alpha, const std::vector<Pt>& poly, float /*feather*/) {
  if (poly.size() < 3) return;
  float y0 = 1e9f, y1 = -1e9f;
  for (const Pt& p : poly) y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  const int ya = std::max(0, static_cast<int>(std::floor(y0))), yb = std::min(alpha.h - 1, static_cast<int>(std::ceil(y1)));
  constexpr int kSub = 4;  // rows sampled per pixel (vertical anti-aliasing); spans are exact horizontally
  std::vector<float> row(static_cast<size_t>(alpha.w));
  std::vector<float> xs;
  for (int y = ya; y <= yb; ++y) {
    std::fill(row.begin(), row.end(), 0.0f);
    bool any = false;
    for (int s = 0; s < kSub; ++s) {
      const float sy = static_cast<float>(y) - 0.5f + (static_cast<float>(s) + 0.5f) / kSub;
      xs.clear();
      for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Pt& a = poly[i];
        const Pt& b = poly[j];
        if ((a.y > sy) != (b.y > sy)) xs.push_back(a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        const float xa = xs[k], xb = xs[k + 1];
        const int x0 = std::max(0, static_cast<int>(std::floor(xa + 0.5f))), x1 = std::min(alpha.w - 1, static_cast<int>(std::floor(xb + 0.5f)));
        for (int x = x0; x <= x1; ++x) {
          const float px = static_cast<float>(x);
          const float cov = std::clamp(std::min(px + 0.5f - xa, xb - (px - 0.5f)), 0.0f, 1.0f);
          row[static_cast<size_t>(x)] += cov / kSub;
          any = true;
        }
      }
    }
    if (!any) continue;
    for (int x = 0; x < alpha.w; ++x) {
      float& v = alpha.at(x, y);
      v = std::max(v, std::min(1.0f, row[static_cast<size_t>(x)]));
    }
  }
}

void stroke(Img& alpha, const std::vector<Pt>& pts, float w0, float w1, float opacity) {
  if (pts.size() < 2 || opacity <= 0) return;
  // Length along the path, for the taper.
  std::vector<float> cum(pts.size(), 0);
  for (size_t i = 1; i < pts.size(); ++i) cum[i] = cum[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
  const float total = std::max(1e-3f, cum.back());
  const float wMax = std::max(w0, w1);
  for (size_t s = 1; s < pts.size(); ++s) {
    const Pt& a = pts[s - 1];
    const Pt& b = pts[s];
    const int xa = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - wMax - 2))), xb = std::min(alpha.w - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + wMax + 2)));
    const int ya = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - wMax - 2))), yb = std::min(alpha.h - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + wMax + 2)));
    for (int y = ya; y <= yb; ++y)
      for (int x = xa; x <= xb; ++x) {
        float t = 0;
        const float d = segDist(static_cast<float>(x), static_cast<float>(y), a, b, &t);
        const float along = (cum[s - 1] + t * (cum[s] - cum[s - 1])) / total;
        const float half = 0.5f * mix(w0, w1, along);
        const float cov = clamp01(half + 0.5f - d) * opacity;  // a pixel-wide anti-aliased edge
        float& v = alpha.at(x, y);
        v = std::max(v, cov);
      }
  }
}

}  // namespace photoplus::toon::filters
