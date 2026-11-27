#include "photoplus/motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <queue>

#include "../art/core.hpp"
#include "photoplus/parallel.hpp"

namespace photoplus::motion {

using namespace photoplus::art::core;

namespace {

constexpr float kTwoPi = 6.28318530718f;

float frac(float v) { return v - std::floor(v); }

// Separable max filter (dilation) of a 1-channel image.
Img dilate(const Img& src, int r) {
  Img a(src.w, src.h, 1), b(src.w, src.h, 1);
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) {
      float v = 0;
      for (int k = std::max(0, x - r); k <= std::min(src.w - 1, x + r); ++k) v = std::max(v, src.at(k, y));
      a.at(x, y) = v;
    }
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) {
      float v = 0;
      for (int k = std::max(0, y - r); k <= std::min(src.h - 1, y + r); ++k) v = std::max(v, a.at(x, k));
      b.at(x, y) = v;
    }
  return b;
}

// Push-pull fill: where `known` is 0 the image is filled smoothly from what
// surrounds it (behind a person, so a moving camera reveals no hole).
Img pushPull(const Img& img, const Img& known) {
  struct Level {
    Img v, w;
  };
  std::vector<Level> levels;
  Level l0{img, known};
  for (size_t i = 0; i < l0.v.d.size(); ++i) l0.v.d[i] *= known.d[i / static_cast<size_t>(img.c)];
  levels.push_back(l0);
  while (levels.back().v.w > 4 && levels.back().v.h > 4) {
    const Level& f = levels.back();
    const int w = (f.v.w + 1) / 2, h = (f.v.h + 1) / 2;
    Level c{Img(w, h, img.c, 0.0f), Img(w, h, 1, 0.0f)};
    for (int y = 0; y < f.v.h; ++y)
      for (int x = 0; x < f.v.w; ++x) {
        const float wt = f.w.at(x, y);
        c.w.at(x / 2, y / 2) += wt;
        for (int k = 0; k < img.c; ++k) c.v.px(x / 2, y / 2)[k] += f.v.px(x, y)[k];
      }
    // Normalise to "premultiplied average" form at this level (weights capped at 1).
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float wt = c.w.at(x, y);
        if (wt > 0)
          for (int k = 0; k < img.c; ++k) c.v.px(x, y)[k] /= wt;
        c.w.at(x, y) = std::min(1.0f, wt);
        for (int k = 0; k < img.c; ++k) c.v.px(x, y)[k] *= c.w.at(x, y);
      }
    levels.push_back(std::move(c));
  }
  // Pull: from coarse to fine, fill what is unknown.
  Img up;
  for (size_t li = levels.size(); li-- > 0;) {
    Level& L = levels[li];
    Img out(L.v.w, L.v.h, img.c);
    for (int y = 0; y < L.v.h; ++y)
      for (int x = 0; x < L.v.w; ++x) {
        const float wt = L.w.at(x, y);
        float coarse[4] = {0, 0, 0, 0};
        if (!up.d.empty())
          for (int k = 0; k < img.c; ++k) coarse[k] = up.sample((static_cast<float>(x) + 0.5f) * 0.5f - 0.5f, (static_cast<float>(y) + 0.5f) * 0.5f - 0.5f, k);
        for (int k = 0; k < img.c; ++k) {
          const float known_v = wt > 0 ? L.v.px(x, y)[k] / wt : 0.0f;
          out.px(x, y)[k] = up.d.empty() ? known_v : mix(coarse[k], known_v, wt);
        }
      }
    up = std::move(out);
  }
  return up;
}

struct Particle {
  float x = 0, y = 0, size = 1, phase = 0, drift = 0;
  int cycles = 1, swirl = 1;
  bool behind = false;
  Rgb3 colour{1, 1, 1};
};

}  // namespace

class Scene {
 public:
  int w = 0, h = 0;
  float G = 1;
  bool bgra = false;
  Img photo;                  // rgb 0..1
  std::vector<uint8_t> alpha; // the photo's own alpha
  Img depth;                  // 0 far .. 1 near
  Img fg;                     // the person (soft), empty: one layer
  Img bg, bgDepth;            // the background with the person filled in
  Img vx, vy;                 // flow over one loop, px
  bool flowing = false;
  Img sky;
  float skyShare = 0;
  float focus = 0.6f;
  Params p;
  std::vector<Particle> particles;
};

namespace {

// The sky: bright or blue, smooth, above the horizon, not the person, joined to the top edge.
Img findSky(const Scene& s) {
  const int sw = std::max(8, s.w / 4), sh = std::max(8, s.h / 4);
  const Img small = resize(s.photo, sw, sh);
  const Img l = blur(lumaOf(small), 1.0f, nullptr);
  const Img fgSmall = s.fg.d.empty() ? Img() : resize(s.fg, sw, sh);
  Img score(sw, sh, 1, 0.0f);
  for (int y = 1; y < sh - 1; ++y)
    for (int x = 1; x < sw - 1; ++x) {
      const float* px = small.px(x, y);
      const float L = l.at(x, y);
      const float grad = std::hypot(l.at(x + 1, y) - l.at(x - 1, y), l.at(x, y + 1) - l.at(x, y - 1));
      float hsv[3];
      rgbToHsv(px, hsv);
      // Sky blue and cyan (a purple or violet wall is not sky), or a bright, colourless overcast.
      const float blueHue = smoothstep(0.47f, 0.52f, hsv[0]) * (1 - smoothstep(0.66f, 0.7f, hsv[0])) * smoothstep(0.08f, 0.18f, hsv[1]);
      const float overcast = smoothstep(0.72f, 0.85f, L) * (1 - smoothstep(0.1f, 0.2f, hsv[1])) * 0.8f;
      const float colour = std::max(blueHue, overcast);
      const float v = static_cast<float>(y) / static_cast<float>(sh);
      float sc = smoothstep(0.3f, 0.55f, L) * (1 - smoothstep(0.025f, 0.07f, grad)) * colour * (1 - smoothstep(0.55f, 0.75f, v));
      if (!fgSmall.d.empty()) sc *= 1 - fgSmall.at(x, y);
      score.at(x, y) = sc;
    }
  score = blur(score, 1.5f, nullptr);
  // Keep what is joined to the top edge.
  Img keep(sw, sh, 1, 0.0f);
  std::queue<std::pair<int, int>> q;
  for (int x = 0; x < sw; ++x)
    for (int y = 0; y < 3; ++y)
      if (score.at(x, y) > 0.4f && keep.at(x, y) == 0) keep.at(x, y) = 1, q.push({x, y});
  while (!q.empty()) {
    auto [x, y] = q.front();
    q.pop();
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
      const int nx = x + dx[k], ny = y + dy[k];
      if (nx < 0 || ny < 0 || nx >= sw || ny >= sh || keep.at(nx, ny) > 0 || score.at(nx, ny) < 0.35f) continue;
      keep.at(nx, ny) = 1;
      q.push({nx, ny});
    }
  }
  for (size_t i = 0; i < keep.d.size(); ++i) keep.d[i] *= smoothstep(0.3f, 0.6f, score.d[i]);
  return blur(resize(keep, s.w, s.h), std::max(1.0f, s.G / 120.0f), nullptr);
}

// The flow: arrows spread over what they point at, anchors hold still, the sky drifts.
void buildFlow(Scene& s, const Input& in) {
  const float scale = std::min(1.0f, 400.0f / static_cast<float>(std::max(s.w, s.h)));
  const int fw = std::max(4, static_cast<int>(static_cast<float>(s.w) * scale)), fh = std::max(4, static_cast<int>(static_cast<float>(s.h) * scale));
  Img vx(fw, fh, 1, 0.0f), vy(fw, fh, 1, 0.0f);
  const float speed = std::clamp(in.params.flowSpeed, 0.0f, 2.0f);
  const float W = static_cast<float>(s.w), H = static_cast<float>(s.h);
  for (int y = 0; y < fh; ++y)
    for (int x = 0; x < fw; ++x) {
      const float px = (static_cast<float>(x) + 0.5f) / scale, py = (static_cast<float>(y) + 0.5f) / scale;
      float sx = 0, sy = 0, sw = 0;
      for (const Arrow& a : in.arrows) {
        const float ax = a.x0 * W, ay = a.y0 * H, bx = a.x1 * W, by = a.y1 * H;
        const float dx = bx - ax, dy = by - ay, len = std::hypot(dx, dy);
        if (len < 1) continue;
        const float u = std::clamp(((px - ax) * dx + (py - ay) * dy) / (len * len), 0.0f, 1.0f);
        const float d = std::hypot(px - (ax + u * dx), py - (ay + u * dy));
        const float R = std::max(len * 0.8f, s.G * 0.07f);
        const float wgt = std::exp(-(d * d) / (R * R));
        sx += wgt * dx, sy += wgt * dy, sw += wgt;
      }
      float ox = sw > 1e-6f ? sx / sw * std::min(1.0f, sw * 1.5f) : 0.0f;
      float oy = sw > 1e-6f ? sy / sw * std::min(1.0f, sw * 1.5f) : 0.0f;
      ox *= speed, oy *= speed;
      if (in.params.sky && !s.sky.d.empty()) {
        const float k = s.sky.sample(px, py) * std::clamp(in.params.skySpeed, -1.0f, 1.0f) * s.G * 0.2f;
        ox += k;
      }
      for (const Anchor& an : in.anchors) {
        const float d = std::hypot(px - an.x * W, py - an.y * H), r = std::max(2.0f, an.r * s.G);
        const float hold = smoothstep(r, r * 1.8f, d);  // nothing within r moves; it eases in beyond
        ox *= hold, oy *= hold;
      }
      vx.at(x, y) = ox, vy.at(x, y) = oy;
    }
  s.vx = resize(vx, s.w, s.h);
  s.vy = resize(vy, s.w, s.h);
  float maxv = 0;
  for (size_t i = 0; i < s.vx.d.size(); ++i) maxv = std::max(maxv, std::hypot(s.vx.d[i], s.vy.d[i]));
  s.flowing = maxv > 0.5f;
}

void makeParticles(Scene& s) {
  const Params& p = s.p;
  const float amt = std::clamp(p.effectAmount, 0.0f, 1.0f);
  Rng rng(p.seed * 7919U + static_cast<uint32_t>(p.effect));
  int n = 0;
  switch (p.effect) {
    case Effect::Snow: n = static_cast<int>(80 + 320 * amt); break;
    case Effect::Rain: n = static_cast<int>(120 + 380 * amt); break;
    case Effect::Bokeh: n = static_cast<int>(12 + 30 * amt); break;
    case Effect::Sparkles: n = static_cast<int>(12 + 48 * amt); break;
    case Effect::Petals: n = static_cast<int>(14 + 50 * amt); break;
    case Effect::Fireflies: n = static_cast<int>(10 + 40 * amt); break;
    default: break;
  }
  // Bokeh takes its colours from the photo's own lights.
  std::vector<Rgb3> lights;
  for (int i = 0; i < 400; ++i) {
    const int x = static_cast<int>(rng.uni() * static_cast<float>(s.w - 1)), y = static_cast<int>(rng.uni() * static_cast<float>(s.h - 1));
    const float* c = s.photo.px(x, y);
    if (luma(c) > 0.55f) lights.push_back({c[0], c[1], c[2]});
  }
  if (lights.empty()) lights.push_back({1.0f, 0.85f, 0.6f});
  for (int i = 0; i < n; ++i) {
    Particle q;
    q.x = rng.uni(), q.y = rng.uni(), q.phase = rng.uni();
    q.behind = rng.uni() < 0.5f;
    q.drift = rng.range(0.4f, 1.0f);
    q.swirl = 1 + static_cast<int>(rng.uni() * 2.0f);
    switch (p.effect) {
      case Effect::Snow:
        q.size = s.G * rng.range(1.0f / 520, 1.0f / 150) * (q.behind ? 0.7f : 1.0f);
        q.cycles = q.behind ? 1 : 2;
        break;
      case Effect::Rain:
        q.size = s.G * rng.range(1.0f / 60, 1.0f / 30);
        q.cycles = 3 + static_cast<int>(rng.uni() * 3);
        break;
      case Effect::Bokeh:
        q.size = s.G * rng.range(1.0f / 34, 1.0f / 12);
        q.colour = lights[static_cast<size_t>(rng.uni() * static_cast<float>(lights.size() - 1))];
        q.behind = true;
        break;
      case Effect::Sparkles: q.size = s.G * rng.range(1.0f / 90, 1.0f / 40), q.cycles = 1 + static_cast<int>(rng.uni() * 3); break;
      case Effect::Petals:
        q.size = s.G * rng.range(1.0f / 110, 1.0f / 60);
        q.cycles = 1;
        q.colour = rng.uni() < 0.5f ? hex(0xF6B7C8) : hex(0xFBD3DD);
        break;
      case Effect::Fireflies:
        q.size = s.G * rng.range(1.0f / 260, 1.0f / 140);
        q.y = rng.range(0.3f, 1.0f);
        q.cycles = 1 + static_cast<int>(rng.uni() * 2);
        break;
      default: break;
    }
    s.particles.push_back(q);
  }
}

// Bilinear sample of the flow-animated image I at q: two phases cross-faded so the loop is seamless.
inline void flowSample(const Scene& s, const Img& I, float qx, float qy, float t, float out[3]) {
  if (!s.flowing) {
    I.sample3(qx, qy, out);
    return;
  }
  const float vx = s.vx.sample(qx, qy), vy = s.vy.sample(qx, qy);
  if (std::fabs(vx) + std::fabs(vy) < 0.05f) {
    I.sample3(qx, qy, out);
    return;
  }
  float a[3], b[3];
  I.sample3(qx - vx * t, qy - vy * t, a);
  I.sample3(qx + vx * (1 - t), qy + vy * (1 - t), b);
  for (int k = 0; k < 3; ++k) out[k] = mix(a[k], b[k], t);
}

struct Cam {
  float zoom = 0, sx = 0, sy = 0, dolly = 0, base = 1;
};

Cam cameraAt(const Params& p, float t) {
  const float A = std::clamp(p.cameraAmount, 0.0f, 1.0f);
  const float e = p.loop ? 0.5f - 0.5f * std::cos(kTwoPi * t) : smoothstep(0.0f, 1.0f, t);
  const float sn = std::sin(kTwoPi * t);
  Cam c;
  switch (p.camera) {
    case Camera::PushIn: c.zoom = 0.16f * A * e; break;
    case Camera::PullOut: c.zoom = 0.16f * A * (1 - e); break;
    case Camera::Orbit:
      c.sx = p.loop ? 0.04f * A * sn : 0.04f * A * (2 * e - 1);
      c.sy = p.loop ? 0.015f * A * (std::cos(kTwoPi * t) - 1) : 0.0f;
      c.zoom = 0.04f * A;
      break;
    case Camera::Pan: c.sx = 0.06f * A * (2 * e - 1), c.zoom = 0.05f * A; break;
    case Camera::DollyZoom: c.dolly = 0.22f * A * e; break;
    case Camera::Float:
      c.sx = 0.016f * A * sn, c.sy = 0.01f * A * std::sin(2 * kTwoPi * t + 1.0f), c.zoom = 0.03f * A + 0.02f * A * e;
      break;
    case Camera::Rise: c.sy = -0.05f * A * (2 * e - 1), c.zoom = 0.06f * A; break;
    default: break;
  }
  // Never show past the photo's edge: a little extra zoom covers the largest move.
  c.base = 1 + 2.2f * (std::fabs(c.sx) + std::fabs(c.sy)) + 0.5f * c.dolly + 0.01f;
  return c;
}

// Where a pixel of the output comes from, for a layer whose depth is `D` (inverse of the camera's move, iterated).
inline void source(const Scene& s, const Cam& c, const Img& D, float x, float y, float& qx, float& qy) {
  const float cx = 0.5f * static_cast<float>(s.w), cy = 0.5f * static_cast<float>(s.h);
  qx = x, qy = y;
  for (int it = 0; it < 4; ++it) {
    const float d = D.d.empty() ? 0.5f : D.sample(qx, qy);
    // Near things zoom more and slide more; the subject's depth is the pivot.
    const float scale = c.base * (1 + c.zoom * (0.55f + 0.9f * d) + c.dolly * std::max(0.0f, s.focus - d) * 1.6f);
    const float rel = s.focus - d;
    const float ox = c.sx * s.G * (0.25f + 2.6f * rel), oy = c.sy * s.G * (0.25f + 2.6f * rel);
    qx = cx + (x - cx - ox) / scale;
    qy = cy + (y - cy - oy) / scale;
  }
}

inline void over(float* p, const float* c, float a) {
  for (int k = 0; k < 3; ++k) p[k] = mix(p[k], c[k], a);
}
inline void screenAdd(float* p, const float* c, float a) {
  for (int k = 0; k < 3; ++k) p[k] = 1 - (1 - p[k]) * (1 - c[k] * a);
}

void drawParticles(const Scene& s, float t, Img& out, const Img& fgOut) {
  const Params& p = s.p;
  const float W = static_cast<float>(s.w), H = static_cast<float>(s.h);
  for (const Particle& q : s.particles) {
    float x = 0, y = 0, a = 1, r = q.size;
    switch (p.effect) {
      case Effect::Snow:
        y = frac(q.y + static_cast<float>(q.cycles) * t) * (H + 4 * r) - 2 * r;
        x = q.x * W + s.G * 0.02f * q.drift * std::sin(kTwoPi * (static_cast<float>(q.swirl) * t + q.phase));
        a = q.behind ? 0.6f : 0.9f;
        break;
      case Effect::Rain:
        y = frac(q.y + static_cast<float>(q.cycles) * t) * (H + 2 * r) - r;
        x = frac(q.x + 0.12f * static_cast<float>(q.cycles) * t) * W;
        a = 0.3f;
        break;
      case Effect::Bokeh:
        y = frac(q.y - t) * (H + 2 * r) - r;
        x = q.x * W + s.G * 0.03f * std::sin(kTwoPi * (t + q.phase));
        a = 0.42f * (0.7f + 0.3f * std::sin(kTwoPi * (t + q.phase)));
        break;
      case Effect::Sparkles: {
        x = q.x * W, y = q.y * H;
        const float tw = std::max(0.0f, std::sin(kTwoPi * (static_cast<float>(q.cycles) * t + q.phase)));
        a = tw * tw * tw;
        break;
      }
      case Effect::Petals:
        y = frac(q.y + t) * (H + 4 * r) - 2 * r;
        x = q.x * W + s.G * 0.05f * std::sin(kTwoPi * (t * static_cast<float>(q.swirl) + q.phase));
        a = 0.9f;
        break;
      case Effect::Fireflies: {
        x = q.x * W + s.G * 0.04f * std::sin(kTwoPi * (static_cast<float>(q.cycles) * t + q.phase));
        y = q.y * H + s.G * 0.03f * std::cos(kTwoPi * (static_cast<float>(q.swirl) * t + q.phase * 1.7f));
        const float glow = 0.5f + 0.5f * std::sin(kTwoPi * (static_cast<float>(q.cycles) * t + q.phase * 3.1f));
        a = glow * glow;
        break;
      }
      default: return;
    }
    if (a <= 0.01f) continue;
    const float ext = p.effect == Effect::Rain ? r : p.effect == Effect::Fireflies ? r * 6 : p.effect == Effect::Sparkles ? r * 1.2f : r * 1.6f;
    const int x0 = std::max(0, static_cast<int>(x - ext)), x1 = std::min(s.w - 1, static_cast<int>(x + ext));
    const int y0 = std::max(0, static_cast<int>(y - ext)), y1 = std::min(s.h - 1, static_cast<int>(y + ext));
    for (int yy = y0; yy <= y1; ++yy)
      for (int xx = x0; xx <= x1; ++xx) {
        const float dx = static_cast<float>(xx) - x, dy = static_cast<float>(yy) - y;
        float cover = 0;
        Rgb3 col{1, 1, 1};
        bool screen = false;
        switch (p.effect) {
          case Effect::Snow: cover = std::exp(-(dx * dx + dy * dy) / (r * r * 0.5f)); break;
          case Effect::Rain: {
            // A slanted streak along the fall.
            const float ux = 0.12f, uy = 1.0f, ul = std::hypot(ux, uy);
            const float along = (dx * ux + dy * uy) / ul, across = (dx * uy - dy * ux) / ul;
            cover = (1 - smoothstep(0.4f, 1.0f, std::fabs(across))) * (1 - smoothstep(r * 0.6f, r, std::fabs(along)));
            col = {0.85f, 0.88f, 0.95f};
            break;
          }
          case Effect::Bokeh: {
            const float d = std::hypot(dx, dy) / r;
            cover = (1 - smoothstep(0.85f, 1.0f, d)) * (0.75f + 0.25f * smoothstep(0.6f, 0.95f, d));
            col = q.colour, screen = true;
            break;
          }
          case Effect::Sparkles: {
            const float cross = std::max(std::exp(-dy * dy / 1.2f) * (1 - smoothstep(0.0f, r, std::fabs(dx))),
                                         std::exp(-dx * dx / 1.2f) * (1 - smoothstep(0.0f, r, std::fabs(dy))));
            cover = std::max(cross, std::exp(-(dx * dx + dy * dy) / (r * r * 0.04f)));
            col = {1.0f, 0.97f, 0.88f}, screen = true;
            break;
          }
          case Effect::Petals: {
            const float ang = q.phase * kTwoPi + kTwoPi * t * static_cast<float>(q.swirl);
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float u = (dx * ca + dy * sa) / r, v = (-dx * sa + dy * ca) / (r * 0.55f);
            cover = 1 - smoothstep(0.8f, 1.0f, u * u + v * v);
            col = q.colour;
            break;
          }
          case Effect::Fireflies: {
            const float d2 = (dx * dx + dy * dy) / (r * r);
            cover = std::exp(-d2) + 0.35f * std::exp(-d2 / 9.0f);
            col = {0.95f, 1.0f, 0.55f}, screen = true;
            break;
          }
          default: break;
        }
        float k = std::min(1.0f, cover * a);
        if (q.behind && !fgOut.d.empty()) k *= 1 - fgOut.at(xx, yy);
        if (k <= 0.003f) continue;
        float* px = out.px(xx, yy);
        if (screen) screenAdd(px, col.data(), k);
        else over(px, col.data(), k);
      }
  }
}

void drawOverlay(const Scene& s, float t, Img& out) {
  const Params& p = s.p;
  const float amt = std::clamp(p.effectAmount, 0.0f, 1.0f);
  const float W = static_cast<float>(s.w), H = static_cast<float>(s.h);
  switch (p.effect) {
    case Effect::LightLeak: {
      const Rgb3 warm = hex(0xFF8A3D), rose = hex(0xFF4F7A);
      const float ax = (0.1f + 0.25f * std::sin(kTwoPi * t)) * W, ay = (0.15f + 0.1f * std::cos(kTwoPi * t)) * H;
      const float bx = (0.95f - 0.2f * std::sin(kTwoPi * t + 1.3f)) * W, by = (0.25f + 0.2f * std::sin(kTwoPi * t)) * H;
      const float pulse = 0.75f + 0.25f * std::sin(kTwoPi * t * 2);
      for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
          const float da = std::hypot(static_cast<float>(x) - ax, static_cast<float>(y) - ay) / (0.55f * s.G);
          const float db = std::hypot(static_cast<float>(x) - bx, static_cast<float>(y) - by) / (0.45f * s.G);
          float* px = out.px(x, y);
          screenAdd(px, warm.data(), 0.55f * amt * pulse * std::exp(-da * da));
          screenAdd(px, rose.data(), 0.4f * amt * pulse * std::exp(-db * db));
        }
      break;
    }
    case Effect::Film: {
      const int frame = static_cast<int>(t * 240.0f);  // the grain changes every frame (and repeats each loop)
      const float flicker = 1 + amt * (0.025f * std::sin(kTwoPi * t * 7) + 0.02f * (hash01(frame, 3, p.seed) - 0.5f));
      for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
          float* px = out.px(x, y);
          const float g = (hash01(x, y + frame * 7919, p.seed) - 0.5f) * 0.09f * amt;
          const float dx = static_cast<float>(x) / W - 0.5f, dy = static_cast<float>(y) / H - 0.5f;
          const float vig = 1 - 0.35f * amt * smoothstep(0.25f, 0.75f, std::sqrt(dx * dx + dy * dy));
          for (int k = 0; k < 3; ++k) px[k] = clamp01((px[k] + g) * flicker * vig);
        }
      // A speck of dust now and then.
      Rng r(static_cast<uint64_t>(frame) * 31U + p.seed);
      if (r.uni() < 0.35f * amt) {
        const float x = r.uni() * W, y = r.uni() * H, rad = s.G * r.range(0.002f, 0.006f);
        for (int yy = std::max(0, static_cast<int>(y - rad)); yy <= std::min(s.h - 1, static_cast<int>(y + rad)); ++yy)
          for (int xx = std::max(0, static_cast<int>(x - rad)); xx <= std::min(s.w - 1, static_cast<int>(x + rad)); ++xx)
            if (std::hypot(static_cast<float>(xx) - x, static_cast<float>(yy) - y) < rad) {
              float* px = out.px(xx, yy);
              for (int k = 0; k < 3; ++k) px[k] *= 0.35f;
            }
      }
      break;
    }
    case Effect::SunRays: {
      const float ox = 0.12f * W, oy = -0.12f * H;
      const Rgb3 sun = hex(0xFFE6B0);
      const float sway = 0.35f * std::sin(kTwoPi * t);
      for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
          const float dx = static_cast<float>(x) - ox, dy = static_cast<float>(y) - oy;
          const float ang = std::atan2(dy, dx), dist = std::hypot(dx, dy) / s.G;
          const float ray = std::pow(fbm(ang * 9.0f + sway, 0.5f, p.seed + 5U, 3), 2.0f);
          const float fall = std::exp(-dist * 1.1f);
          float* px = out.px(x, y);
          screenAdd(px, sun.data(), amt * 0.8f * ray * fall * (0.4f + 0.6f * luma(px)));
        }
      break;
    }
    default: break;
  }
}

}  // namespace

Error create(const Input& in, Scene*& out, const CancellationToken* token) {
  out = nullptr;
  if (in.photo == nullptr || in.photo->empty() || (in.photo->format() != PixelFormat::RGBA8 && in.photo->format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "Photo Motion needs an RGBA or BGRA photo"};
  if (in.photo->width() < 16 || in.photo->height() < 16) return {PP_ERROR_INVALID_ARGUMENT, "The photo is too small"};
  for (const ImageBuffer* m : {in.depth, in.subject})
    if (m != nullptr && (m->empty() || m->format() != PixelFormat::Gray8)) return {PP_ERROR_INVALID_ARGUMENT, "Depth and person masks must be grey images"};
  const Params& p = in.params;
  auto ok = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
  if (static_cast<int>(p.camera) < 0 || static_cast<int>(p.camera) >= kCameraCount || static_cast<int>(p.effect) < 0 ||
      static_cast<int>(p.effect) >= kEffectCount || !ok(p.cameraAmount, 0, 1) || !ok(p.flowSpeed, 0, 2) || !ok(p.skySpeed, -1, 1) ||
      !ok(p.effectAmount, 0, 1))
    return {PP_ERROR_INVALID_ARGUMENT, "A motion setting is out of range"};
  for (const Arrow& a : in.arrows)
    if (!ok(a.x0, -0.5f, 1.5f) || !ok(a.y0, -0.5f, 1.5f) || !ok(a.x1, -0.5f, 1.5f) || !ok(a.y1, -0.5f, 1.5f))
      return {PP_ERROR_INVALID_ARGUMENT, "A motion arrow is out of range"};
  for (const Anchor& a : in.anchors)
    if (!ok(a.x, -0.5f, 1.5f) || !ok(a.y, -0.5f, 1.5f) || !ok(a.r, 0.001f, 1.0f)) return {PP_ERROR_INVALID_ARGUMENT, "An anchor is out of range"};

  auto s = std::make_unique<Scene>();
  s->p = p;
  s->w = in.photo->width(), s->h = in.photo->height();
  s->G = static_cast<float>(std::min(s->w, s->h));
  s->bgra = in.photo->format() == PixelFormat::BGRA8;
  s->photo = Img(s->w, s->h, 3);
  s->alpha.resize(static_cast<size_t>(s->w) * static_cast<size_t>(s->h));
  for (int y = 0; y < s->h; ++y) {
    const uint8_t* row = in.photo->row(y);
    for (int x = 0; x < s->w; ++x) {
      float* px = s->photo.px(x, y);
      px[0] = static_cast<float>(row[x * 4 + (s->bgra ? 2 : 0)]) / 255.0f;
      px[1] = static_cast<float>(row[x * 4 + 1]) / 255.0f;
      px[2] = static_cast<float>(row[x * 4 + (s->bgra ? 0 : 2)]) / 255.0f;
      s->alpha[static_cast<size_t>(y) * static_cast<size_t>(s->w) + static_cast<size_t>(x)] = row[x * 4 + 3];
    }
  }
  auto gray = [&](const ImageBuffer& m) {
    Img g(m.width(), m.height(), 1);
    for (int y = 0; y < m.height(); ++y)
      for (int x = 0; x < m.width(); ++x) g.at(x, y) = static_cast<float>(m.row(y)[x]) / 255.0f;
    return resize(g, s->w, s->h);
  };
  if (in.subject != nullptr) s->fg = blur(gray(*in.subject), std::max(0.8f, s->G / 450.0f), token);
  if (in.depth != nullptr) {
    s->depth = blur(gray(*in.depth), std::max(1.0f, s->G / 300.0f), token);
  } else {
    // No depth: the ground plane (lower is nearer), the person in front.
    s->depth = Img(s->w, s->h, 1);
    for (int y = 0; y < s->h; ++y)
      for (int x = 0; x < s->w; ++x) s->depth.at(x, y) = 0.2f + 0.45f * static_cast<float>(y) / static_cast<float>(s->h);
  }
  if (!s->fg.d.empty()) {
    // The person is one layer; their depth sets the pivot of the move.
    double sum = 0, n = 0;
    for (size_t i = 0; i < s->fg.d.size(); ++i)
      if (s->fg.d[i] > 0.5f) sum += s->depth.d[i], n += 1;
    s->focus = n > 0 ? static_cast<float>(sum / n) : 0.6f;
    if (in.depth == nullptr) {
      s->focus = 0.85f;
      for (size_t i = 0; i < s->fg.d.size(); ++i) s->depth.d[i] = std::max(s->depth.d[i], 0.85f * s->fg.d[i]);
    }
    // What is behind the person, filled from around them (with its depth).
    const int r = std::max(2, static_cast<int>(s->G / 70.0f));
    Img grown = blur(dilate(s->fg, r), static_cast<float>(r) * 0.5f, token);
    Img known(s->w, s->h, 1);
    for (size_t i = 0; i < known.d.size(); ++i) known.d[i] = 1 - smoothstep(0.02f, 0.25f, grown.d[i]);
    s->bg = pushPull(s->photo, known);
    s->bgDepth = pushPull(s->depth, known);
  } else {
    s->focus = 0.55f;
  }
  if (isCancelled(token)) return {PP_ERROR_CANCELLED, "Cancelled"};
  s->sky = findSky(*s);
  double skyw = 0;
  for (float v : s->sky.d) skyw += v;
  s->skyShare = static_cast<float>(skyw / static_cast<double>(s->sky.d.size()));
  buildFlow(*s, in);
  makeParticles(*s);
  out = s.release();
  return {PP_OK, {}};
}

Error render(const Scene& s, float t, ImageBuffer& out, const CancellationToken* token) {
  if (out.width() != s.w || out.height() != s.h || (out.format() != PixelFormat::RGBA8 && out.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "The frame must be the photo's size"};
  if (!std::isfinite(t)) return {PP_ERROR_INVALID_ARGUMENT, "Bad time"};
  t = frac(t);
  const Cam cam = cameraAt(s.p, t);
  Img frame(s.w, s.h, 3);
  Img fgOut = s.fg.d.empty() ? Img() : Img(s.w, s.h, 1, 0.0f);
  const bool layered = !s.fg.d.empty();
  if (!parallelFor(s.h, token, [&](int32_t y0, int32_t y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < s.w; ++x) {
            const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
            float qx, qy, c[3];
            if (!layered) {
              source(s, cam, s.depth, fx, fy, qx, qy);
              flowSample(s, s.photo, qx - 0.5f, qy - 0.5f, t, c);
            } else {
              // The background (filled behind the person) moves with its own depth; the person over it.
              float bx, by, b[3];
              source(s, cam, s.bgDepth, fx, fy, bx, by);
              flowSample(s, s.bg, bx - 0.5f, by - 0.5f, t, b);
              source(s, cam, s.depth, fx, fy, qx, qy);
              const float a = s.fg.sample(qx - 0.5f, qy - 0.5f);
              if (a > 0.002f) {
                flowSample(s, s.photo, qx - 0.5f, qy - 0.5f, t, c);
                for (int k = 0; k < 3; ++k) c[k] = mix(b[k], c[k], a);
              } else {
                for (int k = 0; k < 3; ++k) c[k] = b[k];
              }
              fgOut.at(x, y) = a;
            }
            float* o = frame.px(x, y);
            for (int k = 0; k < 3; ++k) o[k] = c[k];
          }
      }))
    return {PP_ERROR_CANCELLED, "Cancelled"};
  drawParticles(s, t, frame, fgOut);
  drawOverlay(s, t, frame);
  const bool bgra = out.format() == PixelFormat::BGRA8;
  for (int y = 0; y < s.h; ++y) {
    uint8_t* row = out.row(y);
    for (int x = 0; x < s.w; ++x) {
      const float* p = frame.px(x, y);
      auto q = [](float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); };
      row[x * 4 + (bgra ? 2 : 0)] = q(p[0]);
      row[x * 4 + 1] = q(p[1]);
      row[x * 4 + (bgra ? 0 : 2)] = q(p[2]);
      row[x * 4 + 3] = s.alpha[static_cast<size_t>(y) * static_cast<size_t>(s.w) + static_cast<size_t>(x)];
    }
  }
  return {PP_OK, {}};
}

float skyShare(const Scene& scene) { return scene.skyShare; }

void destroy(Scene* scene) { delete scene; }

}  // namespace photoplus::motion
