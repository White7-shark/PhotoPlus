// The live studio (art.hpp): the finished work revealed stroke by stroke
// under a moving pencil or brush.
//
// A plan of strokes is made from the finished work itself, the way the
// work is made:
//  - a drawing: contour lines traced along its strongest edges (one
//    continuous line each, the pencil moving to the nearest next one), then
//    patches of back-and-forth hatching, darkest first;
//  - a painting: a block-in with broad strokes (revealing a soft, blurred
//    underpainting) from the background in, then strokes of the forms,
//    then the finest strokes along the details.
// Each stroke stamps the work through a soft brush onto the blank sheet,
// so every movement reveals exactly what lies beneath; the last moments
// bring in anything left, so the end is the finished work, pixel for pixel.
// Frames forward are incremental: only the new part of the path is stamped.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core.hpp"
#include "photoplus/art.hpp"

namespace photoplus::art {

using namespace core;

namespace {

struct Pt {
  float x = 0, y = 0;
};

struct Mark {
  std::vector<Pt> pts;
  std::vector<float> acc;  // length along the path to each point
  float radius = 4, hard = 0.5f, opacity = 1;
  int layer = 0;            // 0 the work, 1 the underpainting
  float tTravel = 0, t0 = 0, t1 = 0;  // the tool travels from the last mark (lifted), then draws from t0 to t1
  float len() const { return acc.empty() ? 0.0f : acc.back(); }
};

void measure(Mark& m) {
  m.acc.assign(m.pts.size(), 0.0f);
  for (size_t i = 1; i < m.pts.size(); ++i)
    m.acc[i] = m.acc[i - 1] + std::hypot(m.pts[i].x - m.pts[i - 1].x, m.pts[i].y - m.pts[i - 1].y);
}

Pt along(const Mark& m, float l) {
  if (m.pts.empty()) return {};
  if (l <= 0 || m.pts.size() == 1) return m.pts.front();
  if (l >= m.len()) return m.pts.back();
  const auto it = std::upper_bound(m.acc.begin(), m.acc.end(), l);
  const size_t i = static_cast<size_t>(it - m.acc.begin());
  const float seg = m.acc[i] - m.acc[i - 1];
  const float f = seg > 1e-6f ? (l - m.acc[i - 1]) / seg : 0.0f;
  return {mix(m.pts[i - 1].x, m.pts[i].x, f), mix(m.pts[i - 1].y, m.pts[i].y, f)};
}

// The hatching direction each master drew with (y down; Leonardo was left-handed).
float hatchAngle(Artist a) { return a == Artist::Leonardo ? 0.8f : a == Artist::Cezanne ? -1.05f : -0.8f; }

}  // namespace

class Reveal {
 public:
  int w = 0, h = 0;
  bool bgra = false;
  float G = 1;
  RevealTool tool = RevealTool::Pencil;
  std::vector<uint8_t> result, under, blank, canvas;  // packed RGBA rows (w * 4)
  std::vector<Mark> marks;
  float fadeAt = 0.92f;
  Pt entry, exit;
  // Progress: marks before `next` are fully stamped; `stamped` is how far into marks[next].
  size_t next = 0;
  float stamped = -1;  // how far into marks[next] is stamped (-1: not started)
  float lastT = -1;

  void reset() {
    canvas = blank;
    next = 0, stamped = -1, lastT = -1;
  }

  void stamp(float cx, float cy, float r, float hard, float opacity, int layer, float dx, float dy, uint32_t salt) {
    const std::vector<uint8_t>& src = layer == 1 ? under : result;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - r))), x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - r))), y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + r)));
    const float inner = r * hard;
    const bool brush = tool == RevealTool::Brush;
    const float px = -dy, py = dx;  // across the stroke: bristle streaks
    const float phase = static_cast<float>(salt % 97U) * 0.37f;
    for (int y = y0; y <= y1; ++y) {
      uint8_t* c = canvas.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
      const uint8_t* s = src.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
      for (int x = x0; x <= x1; ++x) {
        const float ox = static_cast<float>(x) - cx, oy = static_cast<float>(y) - cy;
        const float d = std::sqrt(ox * ox + oy * oy);
        if (d >= r) continue;
        float a = opacity * (1 - smoothstep(inner, r, d));
        if (brush) a *= 0.82f + 0.18f * std::sin((ox * px + oy * py) / std::max(1.0f, r * 0.11f) + phase);
        if (a <= 0.003f) continue;
        const size_t k = static_cast<size_t>(x) * 4;
        for (int ch = 0; ch < 3; ++ch) {
          const float v = static_cast<float>(c[k + static_cast<size_t>(ch)]) + (static_cast<float>(s[k + static_cast<size_t>(ch)]) - static_cast<float>(c[k + static_cast<size_t>(ch)])) * a;
          c[k + static_cast<size_t>(ch)] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f)));
        }
      }
    }
  }

  // Stamps mark m at its fixed stamp positions (multiples of the step) in (a, b], and its end once b reaches it:
  // the same marks whatever the frame rate.
  void stampRange(const Mark& m, size_t index, float a, float b) {
    const float step = std::max(1.0f, m.radius * (tool == RevealTool::Brush ? 0.3f : 0.4f));
    const float len = m.len();
    int k = a < 0 ? 0 : static_cast<int>(std::floor(a / step)) + 1;
    auto one = [&](float l) {
      const Pt p = along(m, l);
      const Pt q = along(m, std::min(len, l + step));
      const Pt o = along(m, std::max(0.0f, l - step));
      float dx = q.x - o.x, dy = q.y - o.y;
      const float n = std::hypot(dx, dy);
      if (n > 1e-4f) dx /= n, dy /= n;
      else dx = 1, dy = 0;
      stamp(p.x, p.y, m.radius, m.hard, m.opacity, m.layer, dx, dy, static_cast<uint32_t>(index));
    };
    for (; static_cast<float>(k) * step <= b + 1e-4f; ++k) one(static_cast<float>(k) * step);
    if (b >= len && a < len && std::fmod(len, step) > 1e-3f) one(len);
  }

  void advance(float t) {
    if (t < lastT) reset();
    lastT = t;
    while (next < marks.size()) {
      const Mark& m = marks[next];
      if (t < m.t0) break;
      const float f = m.t1 > m.t0 ? clamp01((t - m.t0) / (m.t1 - m.t0)) : 1.0f;
      const float upto = f * m.len();
      if (upto > stamped) {
        stampRange(m, next, stamped, upto);
        stamped = upto;
      }
      if (f < 1) break;
      ++next, stamped = -1;
    }
  }

  // Where the tool is at t, whether it is lifted (0 drawing .. 1 in the air), and the paint on it.
  void toolAt(float t, Pt& p, float& lift, int& layer) const {
    layer = 0;
    if (marks.empty()) {
      p = entry, lift = 1;
      return;
    }
    if (t >= marks.back().t1) {
      const float span = std::max(1e-4f, 1 - marks.back().t1);
      const float f = smoothstep(0.0f, 1.0f, (t - marks.back().t1) / span);
      const Pt a = marks.back().pts.back();
      p = {mix(a.x, exit.x, f), mix(a.y, exit.y, f)}, lift = std::min(1.0f, f * 3);
      return;
    }
    auto it = std::upper_bound(marks.begin(), marks.end(), t, [](float v, const Mark& m) { return v < m.t1; });
    const Mark& m = *it;
    layer = m.layer;
    if (t < m.t0) {
      const Pt from = it == marks.begin() ? entry : std::prev(it)->pts.back();
      const float span = std::max(1e-5f, m.t0 - m.tTravel);
      const float f = smoothstep(0.0f, 1.0f, clamp01((t - m.tTravel) / span));
      p = {mix(from.x, m.pts.front().x, f), mix(from.y, m.pts.front().y, f)};
      // Up and down again: a hop between strokes, higher for a long way.
      const float far = clamp01(std::hypot(m.pts.front().x - from.x, m.pts.front().y - from.y) / (G * 0.25f));
      lift = std::sin(kPi * f) * (0.35f + 0.65f * far);
      return;
    }
    const float f = m.t1 > m.t0 ? clamp01((t - m.t0) / (m.t1 - m.t0)) : 1.0f;
    p = along(m, f * m.len()), lift = 0;
  }
};

namespace {

// ---- reading the work --------------------------------------------------------

void lumaOfRgba(const std::vector<uint8_t>& px, int w, int h, Img& out) {
  out = Img(w, h, 1);
  for (size_t i = 0; i < out.d.size(); ++i)
    out.d[i] = (0.299f * px[i * 4] + 0.587f * px[i * 4 + 1] + 0.114f * px[i * 4 + 2]) / 255.0f;
}

struct Maps {
  Img edge;  // 0..1 edge strength
  Img gx, gy;
  Img diff;  // how different the work is from the blank (0..1)
};

Maps readWork(const Reveal& r, const CancellationToken* token) {
  Maps m;
  Img l;
  lumaOfRgba(r.result, r.w, r.h, l);
  l = blur(l, std::max(0.8f, r.G / 450.0f), token);
  m.edge = Img(r.w, r.h, 1, 0.0f), m.gx = Img(r.w, r.h, 1, 0.0f), m.gy = Img(r.w, r.h, 1, 0.0f);
  for (int y = 1; y < r.h - 1; ++y)
    for (int x = 1; x < r.w - 1; ++x) {
      const float gx = 0.5f * (l.at(x + 1, y) - l.at(x - 1, y)), gy = 0.5f * (l.at(x, y + 1) - l.at(x, y - 1));
      m.gx.at(x, y) = gx, m.gy.at(x, y) = gy, m.edge.at(x, y) = std::hypot(gx, gy);
    }
  std::vector<float> v;
  for (size_t i = 0; i < m.edge.d.size(); i += 7) v.push_back(m.edge.d[i]);
  const size_t k = v.size() * 97 / 100;
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  const float p = std::max(1e-3f, v[k]);
  for (float& e : m.edge.d) e = clamp01(e / p);
  m.diff = Img(r.w, r.h, 1);
  for (size_t i = 0; i < m.diff.d.size(); ++i) {
    int d = 0;
    for (size_t c = 0; c < 3; ++c) d = std::max(d, std::abs(static_cast<int>(r.result[i * 4 + c]) - static_cast<int>(r.blank[i * 4 + c])));
    m.diff.d[i] = static_cast<float>(d) / 255.0f;
  }
  m.diff = blur(m.diff, std::max(1.0f, r.G / 300.0f), token);
  return m;
}

struct Box {
  float x0, y0, x1, y1;
  bool in(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

// Continuous lines along the strongest edges, each traced from a seed both ways.
std::vector<Mark> traceContours(const Reveal& r, const Maps& m, const Box& box, float radius, float maxLen, size_t maxCount, float minEdge) {
  const float cell = std::max(4.0f, r.G / 48.0f);
  const float step = std::max(1.5f, r.G / 260.0f);
  struct Seed {
    float x, y, e;
  };
  std::vector<Seed> seeds;
  for (float cy = box.y0; cy < box.y1; cy += cell)
    for (float cx = box.x0; cx < box.x1; cx += cell) {
      Seed best{0, 0, 0};
      for (int y = static_cast<int>(cy); y < std::min(static_cast<int>(cy + cell), r.h - 1); y += 2)
        for (int x = static_cast<int>(cx); x < std::min(static_cast<int>(cx + cell), r.w - 1); x += 2) {
          const float e = m.edge.at(x, y) * (0.3f + 0.7f * smoothstep(0.02f, 0.12f, m.diff.at(x, y)));
          if (e > best.e) best = {static_cast<float>(x), static_cast<float>(y), e};
        }
      if (best.e > minEdge) seeds.push_back(best);
    }
  std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) { return a.e > b.e; });
  const float vc = std::max(2.0f, r.G / 140.0f);
  const int vw = static_cast<int>(std::ceil(static_cast<float>(r.w) / vc)) + 1, vh = static_cast<int>(std::ceil(static_cast<float>(r.h) / vc)) + 1;
  std::vector<uint8_t> visited(static_cast<size_t>(vw) * static_cast<size_t>(vh), 0);
  auto vis = [&](float x, float y) -> uint8_t& {
    const int ix = std::clamp(static_cast<int>(x / vc), 0, vw - 1), iy = std::clamp(static_cast<int>(y / vc), 0, vh - 1);
    return visited[static_cast<size_t>(iy) * static_cast<size_t>(vw) + static_cast<size_t>(ix)];
  };
  std::vector<std::pair<float, Mark>> found;
  for (const Seed& s : seeds) {
    if (vis(s.x, s.y)) continue;
    std::vector<Pt> both[2];
    for (int dir = 0; dir < 2; ++dir) {
      float x = s.x, y = s.y;
      float px = 0, py = 0;
      float len = 0;
      for (int i = 0; i < 4000 && len < maxLen * 0.5f; ++i) {
        const int ix = std::clamp(static_cast<int>(x), 0, r.w - 1), iy = std::clamp(static_cast<int>(y), 0, r.h - 1);
        float tx = -m.gy.at(ix, iy), ty = m.gx.at(ix, iy);
        const float n = std::hypot(tx, ty);
        if (n < 1e-5f) break;
        tx /= n, ty /= n;
        if (i == 0 && dir == 1) tx = -tx, ty = -ty;
        if (i > 0) {
          if (tx * px + ty * py < 0) tx = -tx, ty = -ty;
          if (tx * px + ty * py < 0.5f) break;  // a sharp turn: the line ends
          tx = 0.6f * tx + 0.4f * px, ty = 0.6f * ty + 0.4f * py;
          const float k = std::hypot(tx, ty);
          tx /= k, ty /= k;
        }
        x += tx * step, y += ty * step, len += step;
        px = tx, py = ty;
        if (!box.in(x, y)) break;
        const int jx = std::clamp(static_cast<int>(x), 0, r.w - 1), jy = std::clamp(static_cast<int>(y), 0, r.h - 1);
        if (m.edge.at(jx, jy) < minEdge * 0.45f) break;
        if (i > 2 && vis(x, y)) break;
        both[dir].push_back({x, y});
      }
    }
    Mark mk;
    for (auto it = both[1].rbegin(); it != both[1].rend(); ++it) mk.pts.push_back(*it);
    mk.pts.push_back({s.x, s.y});
    for (const Pt& p : both[0]) mk.pts.push_back(p);
    for (const Pt& p : mk.pts) vis(p.x, p.y) = 1;
    // Fewer points, gently smoothed.
    std::vector<Pt> sm;
    for (size_t i = 0; i < mk.pts.size(); i += 3) {
      Pt a = mk.pts[i];
      if (i > 0 && i + 1 < mk.pts.size()) a = {(mk.pts[i - 1].x + a.x + mk.pts[i + 1].x) / 3, (mk.pts[i - 1].y + a.y + mk.pts[i + 1].y) / 3};
      sm.push_back(a);
    }
    if (sm.size() > 1 && (mk.pts.size() - 1) % 3 != 0) sm.push_back(mk.pts.back());
    mk.pts = sm;
    measure(mk);
    if (mk.len() < r.G / 45.0f) continue;
    mk.radius = radius, mk.hard = radius < 3.0f ? 0.75f : 0.55f, mk.opacity = 1;
    found.push_back({mk.len() * s.e, std::move(mk)});
  }
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<Mark> out;
  for (size_t i = 0; i < std::min(maxCount, found.size()); ++i) out.push_back(std::move(found[i].second));
  return out;
}

// Back-and-forth hatching over one round patch (parallel lines, alternately drawn).
Mark hatchPatch(float cx, float cy, float half, float angle, float radius) {
  Mark mk;
  const float ca = std::cos(angle), sa = std::sin(angle);
  const float spacing = radius * 1.15f;
  const float R = half;
  bool flip = false;
  for (float off = -R; off <= R; off += spacing) {
    // The line through (cx, cy) + off * normal, along (ca, sa), clipped to the square.
    const float ox = cx - sa * off, oy = cy + ca * off;
    float lo = -1e9f, hi = 1e9f;
    // A round patch (a hand shades in ovals, not squares): the chord of the disc.
    const float d2 = half * half - off * off;
    if (d2 <= 0) continue;
    const float chord = std::sqrt(d2);
    lo = std::max(lo, -chord), hi = std::min(hi, chord);
    if (hi - lo < radius) continue;
    Pt a{ox + ca * lo, oy + sa * lo}, b{ox + ca * hi, oy + sa * hi};
    if (flip) std::swap(a, b);
    mk.pts.push_back(a);
    mk.pts.push_back(b);
    flip = !flip;
  }
  measure(mk);
  mk.radius = radius, mk.hard = 0.5f, mk.opacity = 1;
  return mk;
}

// Broad painted strokes in a cell, following the forms.
void cellStrokes(const Maps& m, float cx, float cy, float cell, float radius, int count, Rng& rng, int layer, float opacity, float hard,
                 std::vector<Mark>& out, const Reveal& r) {
  for (int k = 0; k < count; ++k) {
    float x = cx + rng.range(-0.35f, 0.35f) * cell, y = cy + rng.range(-0.35f, 0.35f) * cell;
    const int ix = std::clamp(static_cast<int>(x), 0, r.w - 1), iy = std::clamp(static_cast<int>(y), 0, r.h - 1);
    float tx = -m.gy.at(ix, iy), ty = m.gx.at(ix, iy);
    float n = std::hypot(tx, ty);
    if (n < 1e-4f) {
      const float a = rng.range(-0.5f, 0.5f) + 0.25f;
      tx = std::cos(a), ty = std::sin(a);
    } else {
      tx /= n, ty /= n;
    }
    const float len = radius * rng.range(2.2f, 3.4f);
    Mark mk;
    // A slight curve: a hand's sweep.
    const float bend = rng.range(-0.25f, 0.25f);
    for (int i = 0; i <= 6; ++i) {
      const float u = (static_cast<float>(i) / 6.0f - 0.5f) * len;
      const float v = bend * len * (0.25f - (static_cast<float>(i) / 6.0f - 0.5f) * (static_cast<float>(i) / 6.0f - 0.5f));
      mk.pts.push_back({x + tx * u - ty * v, y + ty * u + tx * v});
    }
    measure(mk);
    mk.radius = radius * rng.range(0.85f, 1.1f), mk.hard = hard, mk.opacity = opacity, mk.layer = layer;
    out.push_back(std::move(mk));
  }
}

// Orders marks as a hand would: always on to the nearest next stroke (with a
// pull towards `bias`, e.g. the darker patches or the background first).
std::vector<Mark> chain(std::vector<Mark> marks, Pt from, const std::vector<float>* bias, float biasScale) {
  std::vector<Mark> out;
  std::vector<bool> used(marks.size(), false);
  out.reserve(marks.size());
  Pt at = from;
  for (size_t n = 0; n < marks.size(); ++n) {
    size_t best = 0;
    float bestCost = 1e30f;
    bool reverse = false;
    for (size_t i = 0; i < marks.size(); ++i) {
      if (used[i] || marks[i].pts.empty()) continue;
      const float b = bias ? (*bias)[i] * biasScale : 0.0f;
      const float c0 = std::hypot(marks[i].pts.front().x - at.x, marks[i].pts.front().y - at.y) - b;
      const float c1 = std::hypot(marks[i].pts.back().x - at.x, marks[i].pts.back().y - at.y) - b;
      if (c0 < bestCost) bestCost = c0, best = i, reverse = false;
      if (c1 < bestCost) bestCost = c1, best = i, reverse = true;
    }
    if (bestCost >= 1e29f) break;
    used[best] = true;
    Mark mk = std::move(marks[best]);
    if (reverse) {
      std::reverse(mk.pts.begin(), mk.pts.end());
      measure(mk);
    }
    at = mk.pts.back();
    out.push_back(std::move(mk));
  }
  return out;
}

// Gives a phase's marks their times within [ta, tb]: drawing time by length, a quicker hop between them.
void schedule(std::vector<Mark>& marks, size_t first, Pt from, float ta, float tb) {
  constexpr float kHop = 0.3f;  // the hand moves through the air about three times faster than it draws
  float total = 0;
  Pt at = from;
  for (size_t i = first; i < marks.size(); ++i) {
    total += marks[i].len() + kHop * std::hypot(marks[i].pts.front().x - at.x, marks[i].pts.front().y - at.y);
    at = marks[i].pts.back();
  }
  if (total <= 0) return;
  float t = ta;
  at = from;
  for (size_t i = first; i < marks.size(); ++i) {
    const float hop = kHop * std::hypot(marks[i].pts.front().x - at.x, marks[i].pts.front().y - at.y);
    marks[i].tTravel = t;
    marks[i].t0 = t + (tb - ta) * hop / total;
    marks[i].t1 = marks[i].t0 + (tb - ta) * marks[i].len() / total;
    t = marks[i].t1;
    at = marks[i].pts.back();
  }
}

void pack(const ImageBuffer& src, std::vector<uint8_t>& out) {
  const int w = src.width(), h = src.height();
  out.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  for (int y = 0; y < h; ++y) std::memcpy(out.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4, src.row(y), static_cast<size_t>(w) * 4);
}

// ---- the tool -------------------------------------------------------------------

struct Rgba {
  float r, g, b, a;
};

// Fills a convex polygon given in the tool's own coordinates (u along the
// tool from its tip, v across), anti-aliased, coloured per point by `shade`.
template <typename Shade>
void fillConvex(uint8_t* px, int w, int h, size_t stride, bool bgra, const Pt* poly, int n, Pt tip, float ca, float sa, float scale, float soft,
                Shade&& shade) {
  Pt world[16];
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (int i = 0; i < n; ++i) {
    const float u = poly[i].x * scale, v = poly[i].y * scale;
    world[i] = {tip.x + u * ca - v * sa, tip.y + u * sa + v * ca};
    minx = std::min(minx, world[i].x), maxx = std::max(maxx, world[i].x);
    miny = std::min(miny, world[i].y), maxy = std::max(maxy, world[i].y);
  }
  // Orientation (inside on the left or the right of each edge).
  float area = 0;
  for (int i = 0; i < n; ++i) area += world[i].x * world[(i + 1) % n].y - world[(i + 1) % n].x * world[i].y;
  const float sgn = area >= 0 ? 1.0f : -1.0f;
  const int x0 = std::max(0, static_cast<int>(minx - soft - 1)), x1 = std::min(w - 1, static_cast<int>(maxx + soft + 1));
  const int y0 = std::max(0, static_cast<int>(miny - soft - 1)), y1 = std::min(h - 1, static_cast<int>(maxy + soft + 1));
  float nx[16], ny[16], nc[16];
  for (int i = 0; i < n; ++i) {
    const Pt a = world[i], b = world[(i + 1) % n];
    float ex = b.x - a.x, ey = b.y - a.y;
    const float l = std::max(1e-6f, std::hypot(ex, ey));
    ex /= l, ey /= l;
    nx[i] = -ey * sgn, ny[i] = ex * sgn, nc[i] = -(nx[i] * a.x + ny[i] * a.y);
  }
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
      float d = 1e9f;
      for (int i = 0; i < n; ++i) d = std::min(d, nx[i] * fx + ny[i] * fy + nc[i]);
      const float cov = clamp01(d / soft + 0.5f);
      if (cov <= 0) continue;
      const float rx = fx - tip.x, ry = fy - tip.y;
      const float u = (rx * ca + ry * sa) / scale, v = (-rx * sa + ry * ca) / scale;
      const Rgba c = shade(u, v);
      const float a = cov * c.a;
      uint8_t* p = px + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
      const size_t ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;
      p[ri] = static_cast<uint8_t>(std::lround(clamp01(mix(static_cast<float>(p[ri]) / 255.0f, c.r, a)) * 255.0f));
      p[1] = static_cast<uint8_t>(std::lround(clamp01(mix(static_cast<float>(p[1]) / 255.0f, c.g, a)) * 255.0f));
      p[bi] = static_cast<uint8_t>(std::lround(clamp01(mix(static_cast<float>(p[bi]) / 255.0f, c.b, a)) * 255.0f));
    }
}

struct Part {
  std::vector<Pt> poly;
  int kind;
};

// A sharpened yellow pencil (units: the tool's length is about 1.15), tip at the origin.
const std::vector<Part>& pencilParts() {
  static const std::vector<Part> parts = {
      {{{0.0f, 0.0f}, {0.05f, -0.011f}, {0.05f, 0.011f}}, 0},                         // the graphite point
      {{{0.05f, -0.011f}, {0.165f, -0.042f}, {0.165f, 0.042f}, {0.05f, 0.011f}}, 1},  // the sharpened wood
      {{{0.165f, -0.042f}, {1.0f, -0.042f}, {1.0f, 0.042f}, {0.165f, 0.042f}}, 2},     // the painted body
      {{{1.0f, -0.044f}, {1.085f, -0.044f}, {1.085f, 0.044f}, {1.0f, 0.044f}}, 3},     // the ferrule
      {{{1.085f, -0.04f}, {1.15f, -0.036f}, {1.165f, 0.0f}, {1.15f, 0.036f}, {1.085f, 0.04f}}, 4},  // the eraser
  };
  return parts;
}

// A round artist's brush: the loaded bristles, the metal ferrule, a lacquered handle.
const std::vector<Part>& brushParts() {
  static const std::vector<Part> parts = {
      {{{0.0f, 0.0f}, {0.035f, -0.018f}, {0.09f, -0.03f}, {0.15f, -0.027f}, {0.15f, 0.027f}, {0.09f, 0.03f}, {0.035f, 0.018f}}, 10},
      {{{0.15f, -0.027f}, {0.31f, -0.023f}, {0.31f, 0.023f}, {0.15f, 0.027f}}, 11},
      {{{0.31f, -0.023f}, {1.05f, -0.013f}, {1.09f, 0.0f}, {1.05f, 0.013f}, {0.31f, 0.023f}}, 12},
  };
  return parts;
}

Rgba shadePart(int kind, float u, float v, const Rgba& paint) {
  const float across = std::clamp(v / 0.042f, -1.0f, 1.0f);  // -1 .. 1 across the body
  switch (kind) {
    case 0: {  // graphite: dark, a soft sheen
      const float s = 0.22f + 0.12f * std::max(0.0f, -across);
      return {s, s, s + 0.02f, 1};
    }
    case 1: {  // wood, lit from the upper left, darker on the far side
      const float s = 0.92f - 0.18f * std::max(0.0f, across) + 0.04f * std::sin(u * 220.0f);
      return {0.93f * s, 0.78f * s, 0.58f * s, 1};
    }
    case 2: {  // hexagonal body: three facets, with fine edges between them
      const float facet = across < -0.34f ? 1.06f : across < 0.34f ? 0.96f : 0.78f;
      const float edge = std::fabs(std::fabs(across) - 0.34f) < 0.04f ? 0.85f : 1.0f;
      const float s = facet * edge;
      return {0.98f * s, 0.77f * s, 0.14f * s, 1};
    }
    case 3: {  // the ferrule: brushed metal with ridges
      const float s = 0.78f + 0.15f * std::max(0.0f, -across) - 0.12f * (std::sin(u * 900.0f) > 0.6f ? 1.0f : 0.0f);
      return {s, s, 0.97f * s, 1};
    }
    case 4: {  // the eraser
      const float s = 0.95f - 0.12f * std::max(0.0f, across);
      return {0.93f * s, 0.56f * s, 0.55f * s, 1};
    }
    case 10: {  // bristles: the paint at the tip, the hair's own colour further up
      const float load = smoothstep(0.1f, 0.05f, u);
      const float streak = 0.9f + 0.1f * std::sin(v * 700.0f);
      const float s = streak * (1.0f - 0.15f * std::max(0.0f, v / 0.03f));
      return {mix(0.78f, paint.r, load) * s, mix(0.68f, paint.g, load) * s, mix(0.52f, paint.b, load) * s, 1};
    }
    case 11: {  // ferrule
      const float vv = std::clamp(v / 0.025f, -1.0f, 1.0f);
      const float s = 0.62f + 0.35f * (1 - vv * vv) * (vv < 0 ? 1.0f : 0.6f) - (std::fabs(u - 0.28f) < 0.004f ? 0.2f : 0.0f);
      return {s, s, s * 1.02f, 1};
    }
    default: {  // lacquered handle with a highlight
      const float vv = std::clamp(v / 0.02f, -1.0f, 1.0f);
      const float hl = std::exp(-std::pow((vv + 0.45f) / 0.18f, 2.0f));
      const float s = 0.85f - 0.25f * std::max(0.0f, vv);
      return {std::min(1.0f, 0.62f * s + 0.35f * hl), std::min(1.0f, 0.12f * s + 0.35f * hl), std::min(1.0f, 0.1f * s + 0.35f * hl), 1};
    }
  }
}

void drawTool(uint8_t* px, size_t stride, const Reveal& r, Pt tip, float lift, float angle, const Rgba& paint) {
  const int w = r.w, h = r.h;
  const std::vector<Part>& parts = r.tool == RevealTool::Pencil ? pencilParts() : brushParts();
  const float len = r.G * (r.tool == RevealTool::Pencil ? 0.46f : 0.5f);
  const float scale = len * (1 + 0.04f * lift);
  const float ca = std::cos(angle), sa = std::sin(angle);
  // The shadow on the sheet: further and softer as the tool lifts.
  const Pt sh{tip.x + r.G * (0.008f + 0.03f * lift), tip.y + r.G * (0.014f + 0.035f * lift)};
  const float soft = 2.0f + r.G * (0.004f + 0.012f * lift);
  const float shA = 0.26f * (1 - 0.35f * lift);
  for (const Part& p : parts)
    fillConvex(px, w, h, stride, r.bgra, p.poly.data(), static_cast<int>(p.poly.size()), sh, ca, sa, scale, soft, [&](float, float) {
      return Rgba{0.05f, 0.04f, 0.03f, shA};
    });
  for (const Part& p : parts)
    fillConvex(px, w, h, stride, r.bgra, p.poly.data(), static_cast<int>(p.poly.size()), tip, ca, sa, scale, 1.0f,
               [&](float u, float v) { return shadePart(p.kind, u, v, paint); });
}

}  // namespace

Error revealCreate(const ImageBuffer& result, const ImageBuffer& blank, const RevealOptions& o, Reveal*& out, const CancellationToken* token) {
  out = nullptr;
  if (result.empty() || blank.empty() || result.width() != blank.width() || result.height() != blank.height() ||
      result.format() != blank.format() || (result.format() != PixelFormat::RGBA8 && result.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "The work and its blank must be RGBA images of the same size"};
  if (result.width() < 32 || result.height() < 32) return {PP_ERROR_INVALID_ARGUMENT, "The work is too small"};
  if (static_cast<int>(o.tool) < 0 || static_cast<int>(o.tool) > 1 || static_cast<int>(o.artist) < 0 || static_cast<int>(o.artist) >= kArtistCount)
    return {PP_ERROR_INVALID_ARGUMENT, "Unknown tool or artist"};
  auto ok = [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; };
  if (!ok(o.x0) || !ok(o.y0) || !ok(o.x1) || !ok(o.y1) || o.x1 - o.x0 < 0.2f || o.y1 - o.y0 < 0.2f)
    return {PP_ERROR_INVALID_ARGUMENT, "The picture area is out of range"};

  auto* r = new Reveal();
  r->w = result.width(), r->h = result.height();
  r->bgra = result.format() == PixelFormat::BGRA8;
  r->tool = o.tool;
  pack(result, r->result);
  pack(blank, r->blank);
  const Box box{o.x0 * static_cast<float>(r->w), o.y0 * static_cast<float>(r->h), o.x1 * static_cast<float>(r->w), o.y1 * static_cast<float>(r->h)};
  r->G = std::min(box.x1 - box.x0, box.y1 - box.y0);
  const float G = r->G;
  const Maps m = readWork(*r, token);
  if (isCancelled(token)) {
    delete r;
    return {PP_ERROR_CANCELLED, "Cancelled"};
  }
  Rng rng(o.seed == 0 ? 1U : o.seed);
  // The tool comes in from the lower right and leaves that way.
  r->entry = {box.x1 + G * 0.15f, box.y1 + G * 0.05f};
  r->exit = {box.x1 + G * 0.25f, box.y1 + G * 0.2f};
  const Pt centre{(box.x0 + box.x1) * 0.5f, (box.y0 + box.y1) * 0.42f};

  if (o.tool == RevealTool::Pencil) {
    r->fadeAt = 0.9f;
    // 1. Contours.
    std::vector<Mark> lines = traceContours(*r, m, box, std::max(1.6f, G / 260.0f), G / 3.0f, 90, 0.32f);
    lines = chain(std::move(lines), centre, nullptr, 0);
    r->marks = std::move(lines);
    schedule(r->marks, 0, r->entry, 0.035f, 0.42f);
    // 2. Hatching patches, darkest first, each on to the nearest.
    const float cell = G / 7.0f;
    std::vector<Mark> patches;
    std::vector<float> dark;
    const float angle = hatchAngle(o.artist);
    for (float cy = box.y0 + cell * 0.5f; cy < box.y1; cy += cell)
      for (float cx = box.x0 + cell * 0.5f; cx < box.x1; cx += cell) {
        float sum = 0;
        int n = 0;
        for (int y = static_cast<int>(cy - cell / 2); y < static_cast<int>(cy + cell / 2); y += 3)
          for (int x = static_cast<int>(cx - cell / 2); x < static_cast<int>(cx + cell / 2); x += 3)
            if (x >= 0 && y >= 0 && x < r->w && y < r->h) sum += m.diff.at(x, y), ++n;
        const float mean = n ? sum / static_cast<float>(n) : 0;
        if (mean < 0.025f) continue;
        const float jitter = rng.range(-0.12f, 0.12f);
        patches.push_back(hatchPatch(cx + rng.range(-0.1f, 0.1f) * cell, cy + rng.range(-0.1f, 0.1f) * cell, cell * 0.8f, angle + jitter,
                                      std::max(2.5f, cell / 12.0f)));
        dark.push_back(mean);
      }
    const Pt from = r->marks.empty() ? r->entry : r->marks.back().pts.back();
    patches = chain(std::move(patches), from, &dark, G * 0.9f);
    const size_t first = r->marks.size();
    for (Mark& p : patches) r->marks.push_back(std::move(p));
    schedule(r->marks, first, from, r->marks.empty() || first == 0 ? 0.035f : r->marks[first - 1].t1, 0.9f);
  } else {
    r->fadeAt = 0.91f;
    // The underpainting: the work, softened, as broad strokes would lay it in.
    {
      Img rgb(r->w, r->h, 3);
      for (size_t i = 0; i < static_cast<size_t>(r->w) * static_cast<size_t>(r->h); ++i)
        for (size_t c = 0; c < 3; ++c) rgb.d[i * 3 + c] = static_cast<float>(r->result[i * 4 + c]) / 255.0f;
      rgb = blur(rgb, G / 45.0f, token);
      r->under = r->result;
      for (size_t i = 0; i < static_cast<size_t>(r->w) * static_cast<size_t>(r->h); ++i)
        for (size_t c = 0; c < 3; ++c) r->under[i * 4 + c] = static_cast<uint8_t>(std::lround(clamp01(rgb.d[i * 3 + c]) * 255.0f));
    }
    // 1. Block-in, from the background in (furthest from the centre first).
    std::vector<Mark> block;
    const float cA = G / 5.0f;
    for (float cy = box.y0 + cA * 0.5f; cy < box.y1 + cA * 0.25f; cy += cA)
      for (float cx = box.x0 + cA * 0.5f; cx < box.x1 + cA * 0.25f; cx += cA)
        cellStrokes(m, std::min(cx, box.x1), std::min(cy, box.y1), cA, cA * 0.46f, 3, rng, 1, 0.95f, 0.2f, block, *r);
    std::vector<float> far;
    for (const Mark& b : block) far.push_back(std::hypot(b.pts[3].x - centre.x, b.pts[3].y - centre.y));
    block = chain(std::move(block), r->entry, &far, 0.8f);
    r->marks = std::move(block);
    schedule(r->marks, 0, r->entry, 0.03f, 0.33f);
    // 2. The forms.
    std::vector<Mark> forms;
    const float cB = G / 11.0f;
    for (float cy = box.y0 + cB * 0.5f; cy < box.y1; cy += cB)
      for (float cx = box.x0 + cB * 0.5f; cx < box.x1; cx += cB) cellStrokes(m, cx, cy, cB, cB * 0.5f, 2, rng, 0, 0.9f, 0.3f, forms, *r);
    Pt from = r->marks.back().pts.back();
    forms = chain(std::move(forms), from, nullptr, 0.0f);
    size_t first = r->marks.size();
    for (Mark& f : forms) r->marks.push_back(std::move(f));
    schedule(r->marks, first, from, r->marks[first - 1].t1, 0.66f);
    // 3. The finest strokes, along the details.
    std::vector<Mark> fine = traceContours(*r, m, box, std::max(2.0f, G / 150.0f), G / 9.0f, 140, 0.28f);
    from = r->marks.back().pts.back();
    fine = chain(std::move(fine), from, nullptr, 0);
    first = r->marks.size();
    for (Mark& f : fine) r->marks.push_back(std::move(f));
    if (r->marks.size() > first) schedule(r->marks, first, from, r->marks[first - 1].t1, 0.91f);
  }
  // Drop anything without a path.
  r->marks.erase(std::remove_if(r->marks.begin(), r->marks.end(), [](const Mark& k) { return k.pts.size() < 2 || !(k.t1 >= k.t0); }), r->marks.end());
  r->reset();
  out = r;
  return {PP_OK, {}};
}

Error revealFrame(Reveal& r, float t, ImageBuffer& out) {
  if (!std::isfinite(t)) return {PP_ERROR_INVALID_ARGUMENT, "t must be a number"};
  t = std::clamp(t, 0.0f, 1.0f);
  const PixelFormat fmt = r.bgra ? PixelFormat::BGRA8 : PixelFormat::RGBA8;
  if (out.empty() || out.width() != r.w || out.height() != r.h || out.format() != fmt) {
    if (!out.empty() && !out.ownsPixels()) return {PP_ERROR_INVALID_ARGUMENT, "The frame must be the work's size and format"};
    auto img = ImageBuffer::allocate(r.w, r.h, fmt);
    if (!img) return img.error();
    out = std::move(*img);
  }
  r.advance(t);
  // The last moments: anything left comes in, so the end is the finished work.
  const float f = t >= 1 ? 1.0f : smoothstep(r.fadeAt, 1.0f, t);
  const size_t rowBytes = static_cast<size_t>(r.w) * 4;
  for (int y = 0; y < r.h; ++y) {
    const uint8_t* c = r.canvas.data() + static_cast<size_t>(y) * rowBytes;
    uint8_t* d = out.row(y);
    if (f <= 0) {
      std::memcpy(d, c, rowBytes);
      continue;
    }
    const uint8_t* s = r.result.data() + static_cast<size_t>(y) * rowBytes;
    const int fi = static_cast<int>(std::lround(f * 256.0f));
    for (size_t i = 0; i < rowBytes; ++i) d[i] = static_cast<uint8_t>((c[i] * (256 - fi) + s[i] * fi) >> 8);
  }
  if (t < 1) {
    Pt p;
    float lift = 0;
    int layer = 0;
    r.toolAt(t, p, lift, layer);
    // The hand's own sway, and a little lean into the direction of travel.
    Pt q;
    float l2;
    int layer2;
    r.toolAt(std::max(0.0f, t - 0.006f), q, l2, layer2);
    const float vx = (p.x - q.x) / std::max(1.0f, r.G * 0.05f);
    const float angle = -0.98f + 0.06f * std::sin(t * 23.0f) + std::clamp(vx, -1.0f, 1.0f) * 0.08f;
    Rgba paint{0.5f, 0.45f, 0.4f, 1};
    if (r.tool == RevealTool::Brush) {
      // The brush carries the colour it is laying down.
      const int ix = std::clamp(static_cast<int>(p.x), 0, r.w - 1), iy = std::clamp(static_cast<int>(p.y), 0, r.h - 1);
      const std::vector<uint8_t>& s = layer == 1 ? r.under : r.result;
      const size_t k = (static_cast<size_t>(iy) * static_cast<size_t>(r.w) + static_cast<size_t>(ix)) * 4;
      paint = {static_cast<float>(s[k + (r.bgra ? 2 : 0)]) / 255.0f, static_cast<float>(s[k + 1]) / 255.0f, static_cast<float>(s[k + (r.bgra ? 0 : 2)]) / 255.0f, 1};
    }
    // The tool is drawn on the frame only, never on the sheet.
    drawTool(out.data(), static_cast<size_t>(out.stride()), r, p, lift, angle, paint);
  }
  return {PP_OK, {}};
}

int32_t revealStrokeCount(const Reveal& r) { return static_cast<int32_t>(r.marks.size()); }

void revealDestroy(Reveal* r) { delete r; }

}  // namespace photoplus::art
