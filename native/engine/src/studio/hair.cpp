// Hair for the Photoshoot Studio: see studio.hpp. Two things it does:
//
//  - A new colour for the person's own hair: each hair pixel keeps its
//    light relative to the hair's average (so strands, shine and shadow
//    stay), at the new colour's lightness and hue.
//  - A new hairstyle: the person's own hair is taken out of the cut-out
//    (where it lay in front of the body, the body is filled in from around
//    it), and a style is drawn in its place, placed and sized from the face.
//    A style is a shape (built from the skull, the length, the volume and
//    extras such as a bun or an afro) and a texture (straight, wavy, curly,
//    coily, locs, braids, cornrows, waves, stubble...). Strands follow a
//    flow from the parting, shaded darker toward the roots and the face,
//    with a glossy band across the crown; tips are soft or fuzzy by
//    texture. Long hair falls behind the neck and in front of the shoulders.
//
// Everything is worked in linear light. Units for shapes: the face height
// (hairline to chin) is 1, x is measured from the face's centre and y from
// the hairline (down).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "../matte/tools.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

using namespace matte;

namespace {

enum class Tex { Straight, Wavy, Curly, Coily, Locs, Braids, Cornrows, Waves, Stubble, Sleek, Twists, Bald };
enum class Extra { None, TopKnot, Puff, Bantu, ManBun, Ponytail };

struct Style {
  const char* id;
  bool women;
  Tex tex;
  float top;     // volume above the skull
  float side;    // width beyond the skull at the temples
  float len;     // below the chin (negative: short, above the ears)
  float fringe;  // how far bangs come down the forehead (0..0.45)
  float part;    // the parting, -1 (left) .. 1 (right), 0 middle
  float fade;    // 0..1: sides cut close
  Extra extra;
  bool afro;     // a round shape
  float strip;   // mohawk / faux-hawk: half-width of the top strip (0: none)
  bool flat;     // flat top
  bool slick;    // combed back
};

// clang-format off
const std::vector<Style>& styles() {
  static const std::vector<Style> s = {
    // Women
    {"long-straight",    true,  Tex::Straight, 0.03f, 0.04f, 0.95f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"silk-press",       true,  Tex::Sleek,    0.0f,  0.02f, 1.05f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"long-waves",       true,  Tex::Wavy,     0.05f, 0.09f, 0.9f,  0.0f,  0.25f, 0, Extra::None,     false, 0, false, false},
    {"beach-waves",      true,  Tex::Wavy,     0.06f, 0.11f, 0.6f,  0.0f,  0.4f,  0, Extra::None,     false, 0, false, false},
    {"long-curls",       true,  Tex::Curly,    0.1f,  0.16f, 0.8f,  0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"bob",              true,  Tex::Straight, 0.03f, 0.06f, 0.02f, 0.0f,  0.3f,  0, Extra::None,     false, 0, false, false},
    {"bob-bangs",        true,  Tex::Straight, 0.03f, 0.06f, 0.0f,  0.3f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"lob",              true,  Tex::Straight, 0.03f, 0.05f, 0.3f,  0.0f,  0.45f, 0, Extra::None,     false, 0, false, false},
    {"pixie",            true,  Tex::Straight, 0.06f, 0.02f, -1.0f, 0.16f, 0.5f,  0, Extra::None,     false, 0, false, false},
    {"curtain-bangs",    true,  Tex::Straight, 0.04f, 0.06f, 0.7f,  0.2f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"shag",             true,  Tex::Wavy,     0.07f, 0.13f, 0.35f, 0.16f, 0.0f,  0, Extra::None,     false, 0, false, false},
    {"side-swept",       true,  Tex::Straight, 0.04f, 0.05f, 0.6f,  0.22f, 0.8f,  0, Extra::None,     false, 0, false, false},
    {"top-knot",         true,  Tex::Sleek,    0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::TopKnot,  false, 0, false, true},
    {"sleek-ponytail",   true,  Tex::Sleek,    0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::Ponytail, false, 0, false, true},
    {"curly-updo",       true,  Tex::Curly,    0.02f, 0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::TopKnot,  false, 0, false, true},
    {"afro",             true,  Tex::Coily,    0.2f,  0.3f,  0.0f,  0.0f,  0.0f,  0, Extra::None,     true,  0, false, false},
    {"afro-puff",        true,  Tex::Coily,    0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::Puff,     false, 0, false, true},
    {"twist-out",        true,  Tex::Curly,    0.14f, 0.22f, 0.2f,  0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"bantu-knots",      true,  Tex::Twists,   0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::Bantu,    false, 0, false, false},
    {"box-braids",       true,  Tex::Braids,   0.03f, 0.06f, 1.0f,  0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"cornrow-braids",   true,  Tex::Cornrows, 0.0f,  0.0f,  0.8f,  0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"long-locs",        true,  Tex::Locs,     0.06f, 0.08f, 0.9f,  0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"finger-waves",     true,  Tex::Waves,    0.0f,  0.0f,  -1.0f, 0.0f,  0.6f,  0, Extra::None,     false, 0, false, false},
    // Men
    {"buzz-cut",         false, Tex::Stubble,  0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"crew-cut",         false, Tex::Straight, 0.04f, 0.0f,  -1.0f, 0.05f, 0.0f,  0.4f, Extra::None,  false, 0, false, false},
    {"skin-fade",        false, Tex::Straight, 0.05f, 0.0f,  -1.0f, 0.04f, 0.0f,  1, Extra::None,     false, 0, false, false},
    {"high-top-fade",    false, Tex::Coily,    0.36f, 0.0f,  -1.0f, 0.0f,  0.0f,  1, Extra::None,     false, 0, true,  false},
    {"afro-men",         false, Tex::Coily,    0.12f, 0.16f, -0.3f, 0.0f,  0.0f,  0, Extra::None,     true,  0, false, false},
    {"caesar",           false, Tex::Stubble,  0.01f, 0.0f,  -1.0f, 0.08f, 0.0f,  0.3f, Extra::None,  false, 0, false, false},
    {"waves-360",        false, Tex::Waves,    0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0.3f, Extra::None,  false, 0, false, false},
    {"short-locs",       false, Tex::Locs,     0.1f,  0.05f, 0.1f,  0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"shoulder-locs",    false, Tex::Locs,     0.08f, 0.07f, 0.45f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"twists",           false, Tex::Twists,   0.13f, 0.03f, -1.0f, 0.0f,  0.0f,  0.5f, Extra::None,  false, 0, false, false},
    {"sponge-twists",    false, Tex::Coily,    0.08f, 0.0f,  -1.0f, 0.0f,  0.0f,  0.7f, Extra::None,  false, 0, false, false},
    {"cornrows",         false, Tex::Cornrows, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"undercut-slick",   false, Tex::Sleek,    0.08f, 0.0f,  -1.0f, 0.0f,  0.0f,  1, Extra::None,     false, 0, false, true},
    {"pompadour",        false, Tex::Sleek,    0.2f,  0.0f,  -1.0f, 0.0f,  0.0f,  0.6f, Extra::None,  false, 0, false, true},
    {"quiff",            false, Tex::Straight, 0.16f, 0.0f,  -1.0f, 0.04f, 0.0f,  0.5f, Extra::None,  false, 0, false, false},
    {"side-part",        false, Tex::Straight, 0.05f, 0.0f,  -1.0f, 0.0f,  -0.55f, 0.2f, Extra::None, false, 0, false, false},
    {"french-crop",      false, Tex::Straight, 0.04f, 0.0f,  -1.0f, 0.13f, 0.0f,  0.6f, Extra::None,  false, 0, false, false},
    {"curly-top-fade",   false, Tex::Curly,    0.16f, 0.0f,  -1.0f, 0.04f, 0.0f,  1, Extra::None,     false, 0, false, false},
    {"mohawk",           false, Tex::Straight, 0.22f, 0.0f,  -1.0f, 0.0f,  0.0f,  1, Extra::None,     false, 0.1f, false, true},
    {"faux-hawk",        false, Tex::Straight, 0.15f, 0.0f,  -1.0f, 0.0f,  0.0f,  0.7f, Extra::None,  false, 0.18f, false, true},
    {"man-bun",          false, Tex::Sleek,    0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::ManBun,   false, 0, false, true},
    {"long-men",         false, Tex::Straight, 0.04f, 0.05f, 0.35f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
    {"clean-shave",      false, Tex::Bald,     0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  0, Extra::None,     false, 0, false, false},
  };
  return s;
}
// clang-format on

const Style* findStyle(const char* id) {
  if (id == nullptr || id[0] == '\0') return nullptr;
  for (const auto& s : styles())
    if (std::strcmp(s.id, id) == 0) return &s;
  return nullptr;
}

inline float smooth(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
inline float fract(float x) { return x - std::floor(x); }

const std::array<float, 256>& linearLut() {
  static const std::array<float, 256> v = [] {
    std::array<float, 256> t{};
    for (int i = 0; i < 256; ++i) t[static_cast<size_t>(i)] = toLinear(static_cast<float>(i) / 255.0f);
    return t;
  }();
  return v;
}

// Smooth value noise in -1..1 (a hashed lattice, smoothstep interpolation).
float vnoise(float x, float y, uint32_t seed) {
  const auto x0 = static_cast<int32_t>(std::floor(x)), y0 = static_cast<int32_t>(std::floor(y));
  float tx = x - static_cast<float>(x0), ty = y - static_cast<float>(y0);
  tx = tx * tx * (3.0f - 2.0f * tx), ty = ty * ty * (3.0f - 2.0f * ty);
  auto h = [seed](int32_t a, int32_t b) {
    auto v = static_cast<uint32_t>(a) * 0x9E3779B1u ^ static_cast<uint32_t>(b) * 0x85EBCA77u ^ seed * 0xC2B2AE3Du;
    v ^= v >> 15, v *= 0x2C1B3C6Du, v ^= v >> 12, v *= 0x297A2D39u, v ^= v >> 15;
    return static_cast<float>(v & 0xFFFFu) / 32767.5f - 1.0f;
  };
  const float a = h(x0, y0) + (h(x0 + 1, y0) - h(x0, y0)) * tx;
  const float b = h(x0, y0 + 1) + (h(x0 + 1, y0 + 1) - h(x0, y0 + 1)) * tx;
  return a + (b - a) * ty;
}
float fbm(float x, float y, uint32_t seed) {
  return 0.55f * vnoise(x, y, seed) + 0.3f * vnoise(x * 2.03f, y * 2.03f, seed + 1) + 0.15f * vnoise(x * 4.1f, y * 4.1f, seed + 2);
}

// Signed distance (approximate, in the same units) to an axis-aligned ellipse: < 0 inside.
inline float ellipseSd(float x, float y, float cx, float cy, float rx, float ry) {
  const float dx = (x - cx) / rx, dy = (y - cy) / ry;
  return (std::sqrt(dx * dx + dy * dy) - 1.0f) * std::min(rx, ry);
}
inline float boxSd(float x, float y, float x0, float y0, float x1, float y1, float round) {
  const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
  const float hx = 0.5f * (x1 - x0) - round, hy = 0.5f * (y1 - y0) - round;
  const float qx = std::fabs(x - cx) - hx, qy = std::fabs(y - cy) - hy;
  return std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) -
         round;
}

// The parts map's hair (or face skin) as a soft plane at the cut-out's size.
std::vector<float> zoneAt(const ImageBuffer& zones, Zone zone, int32_t W, int32_t H) {
  const int32_t zw = zones.width(), zh = zones.height(), bpp = bytesPerPixel(zones.format());
  const auto id = static_cast<uint8_t>(zone);
  std::vector<float> out(static_cast<size_t>(W) * H);
  for (int32_t y = 0; y < H; ++y) {
    const float fy = std::clamp((static_cast<float>(y) + 0.5f) * static_cast<float>(zh) / static_cast<float>(H) - 0.5f, 0.0f,
                                static_cast<float>(zh - 1));
    const auto y0 = static_cast<int32_t>(fy);
    const int32_t y1 = std::min(zh - 1, y0 + 1);
    const float ty = fy - static_cast<float>(y0);
    for (int32_t x = 0; x < W; ++x) {
      const float fx = std::clamp((static_cast<float>(x) + 0.5f) * static_cast<float>(zw) / static_cast<float>(W) - 0.5f, 0.0f,
                                  static_cast<float>(zw - 1));
      const auto x0 = static_cast<int32_t>(fx);
      const int32_t x1 = std::min(zw - 1, x0 + 1);
      const float tx = fx - static_cast<float>(x0);
      auto at = [&](int32_t xx, int32_t yy) { return zones.row(yy)[xx * bpp] == id ? 1.0f : 0.0f; };
      out[static_cast<size_t>(y) * W + x] =
          (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
    }
  }
  return out;
}

// Push-pull fill: unknown pixels (w = 0) take the colour of known ones
// around them, smoothly, at any distance.
void pushPull(std::vector<V3>& c, std::vector<float>& w, int32_t W, int32_t H) {
  if (W <= 1 && H <= 1) return;
  const int32_t w2 = (W + 1) / 2, h2 = (H + 1) / 2;
  std::vector<V3> c2(static_cast<size_t>(w2) * h2);
  std::vector<float> wt(static_cast<size_t>(w2) * h2, 0.0f);
  for (int32_t y = 0; y < H; ++y)
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x, j = static_cast<size_t>(y / 2) * w2 + x / 2;
      c2[j] = c2[j] + c[i] * w[i], wt[j] += w[i];
    }
  for (size_t j = 0; j < c2.size(); ++j)
    if (wt[j] > 0) c2[j] = c2[j] * (1.0f / wt[j]), wt[j] = std::min(1.0f, wt[j]);
  pushPull(c2, wt, w2, h2);
  for (int32_t y = 0; y < H; ++y)
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      if (w[i] >= 1.0f) continue;
      const V3 coarse = c2[static_cast<size_t>(y / 2) * w2 + x / 2];
      c[i] = w[i] > 0.0f ? mix(coarse, c[i], w[i]) : coarse;
    }
}

struct Geometry {
  float cx, top, unit;  // face centre x, hairline y (pixels); one face height in pixels
  float halfFace;       // half the face width, in face heights
  float skullTop;       // the top of the skull (face heights, above the hairline: negative)
  float skullHalf;      // the skull's half width at the temples (face heights)
};

// One pixel of a style: coverage, colour (linear), and whether it lies
// behind the person (hair behind the neck).
struct HairPx {
  float a = 0.0f;
  V3 c{};
  bool behind = false;
};

// `faceSkin`: the parts map's face skin at this pixel (the face stays visible there, below any fringe).
HairPx shade(const Style& st, const Geometry& g, float X, float Y, V3 base, V3 skin, float faceSkin, uint32_t seed) {
  HairPx out;
  const float a = g.halfFace;
  const float skullCy = 0.2f, skullRx = g.skullHalf, skullRy = skullCy - g.skullTop;
  const bool longHair = st.len > -0.5f;
  // --- the shape (signed distance, < 0 inside) ---
  float d = 1e9f;
  if (st.afro) {
    d = ellipseSd(X, Y, 0.0f, 0.18f - 0.3f * st.top, skullRx + 0.25f + st.side, 0.62f + st.top);
    d = std::max(d, Y - (0.95f + std::max(0.0f, st.len)));
  } else {
    // The cap: the skull with the style's volume.
    const float capRx = skullRx + st.side + 0.012f, capRy = skullRy + 0.5f * st.top + 0.012f, capCy = skullCy - 0.5f * st.top;
    float cap = ellipseSd(X, Y, 0.0f, capCy, capRx, capRy);
    if (st.flat) cap = std::max(cap, -(Y - (-0.30f - st.top)) * 1.0f);  // a flat top
    // Short hair stops at the ears; long hair falls.
    const float earLine = longHair ? 0.35f : 0.38f;
    if (!longHair) {
      // Short sides taper in to the sideburns instead of ending in a block.
      cap = std::max(cap, Y - earLine);
      cap = std::max(cap, std::fabs(X) - (skullRx + st.side + 0.02f - (0.22f * a) * smooth(0.02f, earLine, Y)));
    }
    d = cap;
    if (st.strip > 0.0f) {
      // A mohawk or faux-hawk: a strip with height, the rest close.
      // A mohawk or faux-hawk: a rounded strip with height; the sides are
      // cut close (stubble, below).
      const float stripD = ellipseSd(X, Y, 0.0f, -0.12f - 0.5f * st.top, st.strip, 0.3f + 0.5f * st.top);
      d = std::min(cap, stripD);
    }
    if (longHair) {
      const float bottom = 0.95f + st.len;
      // Flaring a little toward the ends.
      // Falling over the shoulders, fuller toward the ends.
      const float flare = (0.1f + 0.12f * smooth(0.6f, bottom, Y)) * smooth(0.1f, 0.9f, Y) + st.side;
      const float hang = boxSd(X, Y, -(skullRx + flare), 0.1f, skullRx + flare, bottom, 0.14f);
      d = std::min(d, hang);
    }
  }
  // Extras.
  float dx = 1e9f;
  switch (st.extra) {
    case Extra::TopKnot: dx = ellipseSd(X, Y, 0.0f, -0.42f, 0.2f, 0.17f); break;
    case Extra::Puff: dx = ellipseSd(X, Y, 0.0f, -0.4f, 0.36f, 0.3f); break;
    case Extra::ManBun: dx = ellipseSd(X, Y, 0.0f, -0.38f, 0.14f, 0.12f); break;
    case Extra::Ponytail: dx = ellipseSd(X, Y, skullRx * 0.95f, 0.05f, 0.1f, 0.3f); break;
    case Extra::Bantu: {
      const float pts[5][2] = {{-0.3f, -0.18f}, {0.0f, -0.33f}, {0.3f, -0.18f}, {-0.42f, 0.08f}, {0.42f, 0.08f}};
      for (const auto& p : pts) dx = std::min(dx, ellipseSd(X, Y, p[0], p[1], 0.11f, 0.11f));
      break;
    }
    default: break;
  }
  const bool inExtra = dx < 0.03f;
  d = std::min(d, dx);
  // The face stays visible below the fringe line.
  const float face = ellipseSd(X, Y, 0.0f, 0.58f, a * 1.0f, 0.62f);
  const float fringeLine = std::max(0.0f, st.fringe);
  const float fringeEdge = fringeLine + 0.025f * fbm(X * 30.0f, 0.0f, seed + 9);
  float faceCut = 1.0f;
  if (Y > fringeEdge || st.fringe <= 0.0f) faceCut = 1.0f - smooth(0.3f, 0.75f, faceSkin);
  // The hairline: over the face, hair stops at the top of the forehead (or
  // at the fringe), whatever the parts map makes of a forehead in shadow.
  {
    const float over = 1.0f - smooth(a * 0.8f, a * 1.0f, std::fabs(X));
    const float u = X / std::max(a, 1e-3f);
    const float line = st.fringe > 0.0f ? fringeEdge : -0.03f + 0.12f * u * u + 0.01f * vnoise(X * 60.0f, 0.0f, seed + 10);
    faceCut = std::min(faceCut, 1.0f - over * smooth(line + 0.006f, line + 0.036f, Y) * (Y < 0.95f ? 1.0f : 0.0f));
  }
  // The parts below the chin, in front of the neck and chest, are behind.
  out.behind = Y > 0.92f && std::fabs(X) < a * 0.95f && !inExtra;
  if (st.extra == Extra::Ponytail && dx < 0.0f) out.behind = true;

  // --- texture ---
  const float px = st.part * a * 0.8f;
  const float crownY = -0.3f - st.top;
  const float ddx = X - px, ddy = Y - crownY;
  const float r = std::sqrt(ddx * ddx + ddy * ddy);
  const float ang = std::atan2(ddx, std::max(ddy, 1e-3f));
  const float t = longHair ? smooth(0.15f, 0.6f, Y) : 0.0f;
  float S = mixf(ang * 0.45f, X, t);  // across the strands
  float L = mixf(r, Y, t);            // along them
  if (st.slick) S = X * (0.6f + 0.4f * std::fabs(ddy) / std::max(r, 1e-3f)), L = -Y;
  float strand = 0.0f, alphaMul = 1.0f, scalp = 0.0f;
  float fuzz = 0.004f, jitter = 0.006f, gloss = 0.5f;
  switch (st.tex) {
    case Tex::Straight:
      strand = 0.6f * vnoise(S * 140.0f, L * 4.0f, seed) + 0.4f * vnoise(S * 380.0f, L * 9.0f, seed + 3);
      break;
    case Tex::Sleek:
      strand = 0.7f * vnoise(S * 120.0f, L * 3.0f, seed) + 0.3f * vnoise(S * 300.0f, L * 6.0f, seed + 3);
      gloss = 0.9f, jitter = 0.003f;
      break;
    case Tex::Wavy: {
      const float w = S + 0.035f * std::sin(L * 16.0f + 2.0f * vnoise(S * 8.0f, 0.0f, seed + 5));
      strand = 0.6f * vnoise(w * 130.0f, L * 4.0f, seed) + 0.4f * vnoise(w * 340.0f, L * 8.0f, seed + 3);
      jitter = 0.012f;
      break;
    }
    case Tex::Curly: {
      const float w = S + 0.05f * std::sin(L * 38.0f + 3.0f * vnoise(S * 12.0f, L * 3.0f, seed + 5));
      const float clump = fbm(X * 22.0f, Y * 22.0f, seed + 6);
      strand = 0.5f * vnoise(w * 120.0f, L * 10.0f, seed) + 0.5f * clump;
      fuzz = 0.02f, jitter = 0.03f, gloss = 0.35f;
      break;
    }
    case Tex::Coily: {
      const float fine = fbm(X * 140.0f, Y * 140.0f, seed);
      const float clump = fbm(X * 28.0f, Y * 28.0f, seed + 6);
      strand = 0.55f * fine + 0.45f * clump;
      fuzz = 0.025f, jitter = 0.035f, gloss = 0.2f;
      break;
    }
    case Tex::Locs:
    case Tex::Braids:
    case Tex::Twists: {
      const float freq = st.tex == Tex::Locs ? 13.0f : st.tex == Tex::Braids ? 22.0f : 16.0f;
      const float q = S * freq + 0.15f * vnoise(L * 3.0f, S * 2.0f, seed + 7);
      const float f = fract(q);
      const float cyl = std::sqrt(std::max(0.0f, 1.0f - (2.0f * f - 1.0f) * (2.0f * f - 1.0f)));
      float pattern;
      if (st.tex == Tex::Locs) {
        pattern = 0.6f * fbm(std::floor(q) * 3.1f, L * 35.0f, seed + 8);
      } else {
        // Braids and twists: the strands cross the rope at an angle.
        pattern = std::fabs(fract(L * (st.tex == Tex::Braids ? 40.0f : 30.0f) + (f < 0.5f ? f : 1.0f - f) * 1.2f) - 0.5f) * 2.0f - 0.5f;
      }
      strand = cyl * 1.3f - 0.6f + 0.35f * pattern;
      alphaMul = smooth(0.0f, 0.18f, cyl) * 0.25f + 0.75f;
      gloss = 0.25f, jitter = 0.012f;
      break;
    }
    case Tex::Cornrows: {
      // Rows running from the hairline back over the head, braided.
      const float q = X * 15.0f;
      const float f = fract(q);
      const float cyl = std::sqrt(std::max(0.0f, 1.0f - (2.0f * f - 1.0f) * (2.0f * f - 1.0f)));
      const float chev = std::fabs(fract(Y * 42.0f + (f < 0.5f ? f : 1.0f - f) * 1.2f) - 0.5f) * 2.0f - 0.5f;
      strand = cyl * 1.3f - 0.6f + 0.35f * chev;
      // The scalp shows between the rows.
      if (Y < 0.9f && cyl < 0.25f) scalp = 1.0f - smooth(0.12f, 0.25f, cyl);
      gloss = 0.3f;
      break;
    }
    case Tex::Waves: {
      const float q = r * 55.0f + 0.6f * vnoise(X * 6.0f, Y * 6.0f, seed + 4);
      strand = 0.75f * std::sin(q * 6.2832f) + 0.25f * vnoise(X * 200.0f, Y * 200.0f, seed);
      gloss = 0.8f;
      break;
    }
    case Tex::Stubble:
    case Tex::Bald: break;
  }
  // Fade: the sides cut close, short hair over skin.
  float stubbleMix = 0.0f;
  if (st.tex == Tex::Stubble) stubbleMix = 1.0f;
  if (st.fade > 0.0f && !inExtra) {
    const float sideness = smooth(a * 0.55f, a * 0.95f, std::fabs(X)) * smooth(-0.22f, 0.05f, Y);
    const float shortBelow = st.strip > 0.0f ? smooth(st.strip, st.strip * 1.6f, std::fabs(X)) : 0.0f;
    stubbleMix = std::max(stubbleMix, st.fade * std::max(sideness, shortBelow));
  }
  // --- light ---
  const float dFace = face;
  const float ao = mixf(0.62f, 1.0f, smooth(0.0f, 0.1f, dFace)) * mixf(0.8f, 1.0f, smooth(1.4f, 0.5f, Y));
  const float rr = std::sqrt((X / (skullRx + st.side)) * (X / (skullRx + st.side)) +
                             ((Y - skullCy + 0.5f * st.top) / (skullRy + 0.5f * st.top)) * ((Y - skullCy + 0.5f * st.top) / (skullRy + 0.5f * st.top)));
  const float sheenBand = std::exp(-((rr - 0.78f) / 0.11f) * ((rr - 0.78f) / 0.11f)) * smooth(0.35f, -0.25f, Y);
  const float sheenLong = longHair ? std::exp(-((Y - 0.55f) / 0.12f) * ((Y - 0.55f) / 0.12f)) * 0.5f : 0.0f;
  const float sheen = gloss * std::max(sheenBand, sheenLong) * (0.55f + 0.45f * std::max(0.0f, strand));
  // Light from above: the crown a little brighter than the sides.
  float light = std::clamp(0.62f + 0.38f * strand, 0.15f, 1.3f) * ao * mixf(0.85f, 1.12f, smooth(0.45f, -0.4f, Y));
  V3 c = base * light;
  const V3 highlight = mix(base * 4.0f + grey(0.02f), grey(lum(base) * 3.0f + 0.05f), 0.4f);
  c = c + highlight * (0.55f * sheen);
  if (st.tex == Tex::Bald) {
    // A shaved head: the skin of the face, rounded by the light.
    const float round = std::sqrt(std::max(0.0f, 1.0f - rr * rr));
    c = skin * (0.6f + 0.25f * round) + grey(0.05f * std::exp(-((rr - 0.55f) / 0.18f) * ((rr - 0.55f) / 0.18f)) * smooth(0.3f, -0.2f, Y));
  } else if (stubbleMix > 0.0f) {
    const float dots = vnoise(X * 420.0f, Y * 420.0f, seed + 11) * 0.5f + 0.5f;
    const float dots2 = std::pow(std::max(0.0f, vnoise(X * 900.0f, Y * 900.0f, seed + 14) * 0.5f + 0.5f), 2.0f);
    // Close-cut hair: scalp with dark points of hair in it.
    const V3 stub = mix(skin * 0.62f, base * 0.9f, std::clamp(0.35f + 0.25f * dots + 0.45f * dots2, 0.0f, 1.0f));
    c = mix(c, stub * mixf(0.85f, 1.0f, std::sqrt(std::max(0.0f, 1.0f - rr * rr))), stubbleMix);
    fuzz = mixf(fuzz, 0.012f, stubbleMix), jitter = mixf(jitter, 0.004f, stubbleMix);
  }
  if (scalp > 0.0f) c = mix(c, skin * 0.85f, scalp);
  // --- edge ---
  d += jitter * vnoise(X * 45.0f, Y * 45.0f, seed + 12) + 0.5f * jitter * vnoise(X * 140.0f, Y * 140.0f, seed + 13);
  if (longHair && (st.tex == Tex::Straight || st.tex == Tex::Sleek || st.tex == Tex::Wavy)) {
    // Tips: strands end at different lengths.
    const float bottom = 0.95f + st.len;
    d += 0.05f * (0.5f - 0.5f * strand) * smooth(bottom - 0.2f, bottom, Y);
  }
  float alpha = smooth(fuzz, -fuzz, d) * alphaMul * faceCut;
  if (stubbleMix > 0.0f) alpha *= mixf(1.0f, 0.92f, stubbleMix);
  out.a = std::clamp(alpha, 0.0f, 1.0f);
  out.c = V3{std::max(c.r, 0.0f), std::max(c.g, 0.0f), std::max(c.b, 0.0f)};
  return out;
}

}  // namespace

const std::vector<HairStyleInfo>& hairStyles() {
  static const std::vector<HairStyleInfo> list = [] {
    std::vector<HairStyleInfo> v;
    for (const auto& s : styles()) v.push_back({s.id, s.women});
    return v;
  }();
  return list;
}

Error restyleHair(ImageBuffer& cutout, const ImageBuffer& zones, const HairParams& params, const CancellationToken* token) {
  ScopedTimer timer("studio.hair");
  if (!isColour(cutout)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Hair needs an RGBA cut-out");
  if (zones.width() < 2 || zones.height() < 2) return makeError(PP_ERROR_INVALID_ARGUMENT, "Hair needs the parts map");
  const Style* st = findStyle(params.style);
  if (params.style != nullptr && params.style[0] != '\0' && st == nullptr) return makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown hairstyle");
  if (st == nullptr && !params.recolor) return ok();
  const int32_t W = cutout.width(), H = cutout.height();
  const Rgb o = offsets(cutout);
  const std::array<float, 256>& Lut = linearLut();
  const size_t n = static_cast<size_t>(W) * H;
  std::vector<float> hair = zoneAt(zones, Zone::Hair, W, H);
  // Strands run a little past the coarse parts map: hair near hair counts.
  {
    Plane p;
    auto pr = Plane::allocate(W, H);
    if (!pr) return pr.error();
    p = std::move(pr).value();
    for (size_t i = 0; i < n; ++i) p.data()[i] = hair[i];
    auto b = blurred(p, std::max(1.0f, 0.006f * static_cast<float>(std::min(W, H))), token);
    if (!b) return b.error();
    for (size_t i = 0; i < n; ++i) hair[i] = std::max(hair[i], smooth(0.08f, 0.4f, b->data()[i]));
  }
  const std::vector<float> faceSkin = zoneAt(zones, Zone::FaceSkin, W, H);
  // The person's own hair colour and lightness (linear), and their skin's.
  double hr = 0, hg = 0, hb = 0, hl = 0, hn = 0, sr = 0, sg = 0, sb = 0, sn = 0;
  for (int32_t y = 0; y < H; ++y) {
    const uint8_t* p = cutout.row(y);
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      const uint8_t* q = p + x * 4;
      if (q[3] < 200) continue;
      const V3 c{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]};
      if (hair[i] > 0.8f && faceSkin[i] < 0.2f) hr += c.r, hg += c.g, hb += c.b, hl += std::log(std::max(lum(c), 1e-4f)), hn += 1;
      if (faceSkin[i] > 0.8f) sr += c.r, sg += c.g, sb += c.b, sn += 1;
    }
  }
  const V3 ownHair = hn > 30 ? V3{static_cast<float>(hr / hn), static_cast<float>(hg / hn), static_cast<float>(hb / hn)} : V3{0.03f, 0.02f, 0.015f};
  const float ownLogL = hn > 30 ? static_cast<float>(hl / hn) : std::log(0.02f);
  const V3 skin = sn > 30 ? V3{static_cast<float>(sr / sn), static_cast<float>(sg / sn), static_cast<float>(sb / sn)} : V3{0.25f, 0.15f, 0.1f};
  const V3 newColour = params.recolor ? V3{Lut[params.color[0]], Lut[params.color[1]], Lut[params.color[2]]} : ownHair;
  const float strength = std::clamp(params.strength, 0.0f, 1.0f);

  if (st == nullptr) {
    // A new colour for the person's own hair. Each pixel keeps its light
    // relative to the hair's average; the average becomes the new colour's.
    const float lt = std::max(lum(newColour), 1e-4f);
    const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
      for (int32_t y = b0; y < b1; ++y) {
        uint8_t* p = cutout.row(y);
        for (int32_t x = 0; x < W; ++x) {
          const size_t i = static_cast<size_t>(y) * W + x;
          uint8_t* q = p + x * 4;
          const float m = hair[i] * (1.0f - smooth(0.3f, 0.8f, faceSkin[i])) * strength;
          if (m <= 0.0f || q[3] == 0) continue;
          const V3 c{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]};
          const float rel = std::exp(std::clamp(std::log(std::max(lum(c), 1e-4f)) - ownLogL, -2.5f, 2.5f));
          // A colour swatch is the hair's light side: the average sits a
          // little below it, and the shadows between strands stay deep (more
          // so in light colours, which would otherwise read as flat paint).
          float relShown = std::pow(rel, lt > 0.15f ? 1.5f : 1.2f);
          // Brightly lit hair stays brighter, softly, instead of flat white.
          if (relShown > 1.0f) relShown = 1.0f + (relShown - 1.0f) / (1.0f + 1.2f * (relShown - 1.0f));
          V3 outc = newColour * (0.62f * relShown);
          const float mx = std::max({outc.r, outc.g, outc.b});
          if (mx > 1.0f) outc = outc * (1.0f / mx);
          outc = mix(c, outc, m);
          q[o.r] = toByte(toGamma(outc.r)), q[o.g] = toByte(toGamma(outc.g)), q[o.b] = toByte(toGamma(outc.b));
        }
      }
    });
    return done ? ok() : cancelledError();
  }

  // A new style needs the face.
  if (!(params.faceRight - params.faceLeft > 0.01f && params.faceBottom - params.faceTop > 0.01f)) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "A hairstyle needs a face in the photo");
  }
  Geometry g{};
  g.cx = 0.5f * (params.faceLeft + params.faceRight) * static_cast<float>(W);
  g.top = params.faceTop * static_cast<float>(H);
  g.unit = std::max(4.0f, (params.faceBottom - params.faceTop) * static_cast<float>(H));
  g.halfFace = 0.5f * (params.faceRight - params.faceLeft) * static_cast<float>(W) / g.unit;
  const float chinY = params.faceBottom * static_cast<float>(H);
  // The head's own top and width, from the person's outline (with their
  // own hair, so only ever tightened toward an average skull: a big afro
  // does not make a big skull, a photo from below does make a lower one).
  {
    float topY = 1e9f;
    for (int32_t x = static_cast<int32_t>(g.cx - 0.5f * g.halfFace * g.unit); x <= static_cast<int32_t>(g.cx + 0.5f * g.halfFace * g.unit); ++x) {
      if (x < 0 || x >= W) continue;
      for (int32_t y = 0; y < H; ++y)
        if (cutout.row(y)[x * 4 + 3] > 127) {
          topY = std::min(topY, static_cast<float>(y));
          break;
        }
    }
    const float ownTop = (topY - g.top) / g.unit;
    g.skullTop = std::clamp(ownTop + 0.03f, -0.36f, -0.16f);
    const auto row = static_cast<int32_t>(std::clamp(g.top + 0.1f * g.unit, 0.0f, static_cast<float>(H - 1)));
    int32_t l = W, r = -1;
    for (int32_t x = 0; x < W; ++x)
      if (cutout.row(row)[x * 4 + 3] > 127) l = std::min(l, x), r = std::max(r, x);
    const float ownHalf = r > l ? 0.5f * static_cast<float>(r - l) / g.unit : g.halfFace * 1.12f;
    g.skullHalf = std::clamp(ownHalf, g.halfFace * 1.0f, g.halfFace * 1.12f);
  }

  // 1. Take the person's own hair out. Hair in front of the body (falling
  // over the shoulders or chest) is filled in with the body around it;
  // anywhere else it simply goes.
  std::vector<V3> col(n);
  std::vector<float> alpha(n), known(n);
  for (int32_t y = 0; y < H; ++y) {
    const uint8_t* p = cutout.row(y);
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      const uint8_t* q = p + x * 4;
      col[i] = V3{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]};
      alpha[i] = static_cast<float>(q[3]) / 255.0f;
      known[i] = (alpha[i] > 0.9f && hair[i] < 0.05f) ? 1.0f : 0.0f;
    }
  }
  std::vector<uint8_t> front(n, 0);
  for (int32_t y = 0; y < H; ++y) {
    if (static_cast<float>(y) < chinY) continue;
    int32_t l = W, r = -1;
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      if (alpha[i] > 0.5f && hair[i] < 0.3f) l = std::min(l, x), r = std::max(r, x);
    }
    const int32_t margin = std::max<int32_t>(2, static_cast<int32_t>(0.01f * static_cast<float>(W)));
    for (int32_t x = l + margin; x <= r - margin; ++x) front[static_cast<size_t>(y) * W + x] = 1;
  }
  // Above the chin, everything that is not face, skin, clothes or jewellery
  // goes too: the fuzzy outer strands of big hair are often beyond the
  // parts map's hair.
  std::vector<float> keep(n, 0.0f);
  {
    const auto body = zoneAt(zones, Zone::BodySkin, W, H), cloth = zoneAt(zones, Zone::Clothes, W, H), acc = zoneAt(zones, Zone::Accessory, W, H);
    auto pr = Plane::allocate(W, H);
    if (!pr) return pr.error();
    for (size_t i = 0; i < n; ++i) pr->data()[i] = std::max({faceSkin[i], body[i], cloth[i], acc[i]});
    auto b = blurred(*pr, std::max(1.0f, 0.004f * static_cast<float>(std::min(W, H))), token);
    if (!b) return b.error();
    for (size_t i = 0; i < n; ++i) keep[i] = std::max(pr->data()[i], smooth(0.2f, 0.6f, b->data()[i]));
  }
  std::vector<V3> fill = col;
  std::vector<float> fw = known;
  pushPull(fill, fw, W, H);
  for (size_t i = 0; i < n; ++i) {
    const float m = hair[i] * (1.0f - smooth(0.3f, 0.8f, faceSkin[i]));
    if (m <= 0.0f || static_cast<float>(i / static_cast<size_t>(W)) < chinY) continue;
    if (front[i]) {
      col[i] = mix(col[i], fill[i], m);
      alpha[i] = std::max(alpha[i], m);
    } else {
      alpha[i] *= 1.0f - m;
    }
  }
  // The face itself (below the hairline, between the cheeks) always stays,
  // whatever the parts map makes of a forehead in shadow.
  auto faceKept = [&](int32_t x, int32_t y) {
    const float X = (static_cast<float>(x) + 0.5f - g.cx) / g.unit, Y = (static_cast<float>(y) + 0.5f - g.top) / g.unit;
    const float u = X / std::max(g.halfFace, 1e-3f);
    const float line = -0.03f + 0.12f * u * u;
    return (1.0f - smooth(0.8f, 1.0f, std::fabs(u))) * smooth(line - 0.03f, line + 0.005f, Y);
  };
  for (int32_t y = 0; y < H && static_cast<float>(y) < chinY; ++y)
    for (int32_t x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      const float gone = keep[i] * (1.0f - hair[i] * (1.0f - smooth(0.3f, 0.8f, faceSkin[i])));
      alpha[i] *= mixf(gone, 1.0f, faceKept(x, y));
    }

  // 2. Draw the style: behind the person where it falls behind the neck,
  // over them everywhere else.
  const float inv = 1.0f / g.unit;
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      uint8_t* p = cutout.row(y);
      for (int32_t x = 0; x < W; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        const float X = (static_cast<float>(x) + 0.5f - g.cx) * inv, Y = (static_cast<float>(y) + 0.5f - g.top) * inv;
        V3 c = col[i];
        float a = alpha[i];
        if (X > -2.5f && X < 2.5f && Y > -1.6f && Y < 3.2f) {
          const HairPx h = shade(*st, g, X, Y, mix(ownHair, newColour, params.recolor ? strength : 1.0f), skin, faceSkin[i], params.seed);
          if (h.a > 0.0f) {
            if (h.behind) {
              // Person over hair.
              const float ao = a + h.a * (1.0f - a);
              if (ao > 0.0f) c = (c * a + h.c * (h.a * (1.0f - a))) * (1.0f / ao);
              a = ao;
            } else {
              const float ao = h.a + a * (1.0f - h.a);
              if (ao > 0.0f) c = (h.c * h.a + c * (a * (1.0f - h.a))) * (1.0f / ao);
              a = ao;
            }
          }
        }
        uint8_t* q = p + x * 4;
        q[o.r] = toByte(toGamma(c.r)), q[o.g] = toByte(toGamma(c.g)), q[o.b] = toByte(toGamma(c.b)), q[3] = toByte(a);
      }
    }
  });
  return done ? ok() : cancelledError();
}

}  // namespace photoplus::studio
