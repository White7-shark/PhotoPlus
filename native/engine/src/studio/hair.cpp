// Hair for the Photoshoot Studio: see studio.hpp.
//
//  - A new colour for the person's own hair: each hair pixel keeps its light
//    relative to the hair's average (strands, shine and shadow stay); the
//    shadows keep some of the natural colour (dye shows least there), the
//    highlights stay soft and a little less saturated, as dyed hair does.
//  - A new hairstyle: the person's own hair is taken out of the cut-out
//    (where it lay in front of the body, the body is filled in from around
//    it), and a style is drawn in its place, fitted to the head and face:
//      strands  - thousands of fine strands traced along the style's flow,
//                 lit as hair fibres are (Kajiya-Kay: a white highlight along
//                 the strands and a coloured one below it), deeper strands
//                 darker, coherent waves or per-strand curls;
//      coils    - tight coils for afros and puffs, fuzzy at the edge;
//      ropes    - locs, braids, twists, cornrows and knots as rounded ropes
//                 with a fibrous surface (and stray fibres on locs);
//      dome     - a shaved head: the person's own skin texture (from their
//                 forehead) on a rounded, lit scalp, blended into it.
//    The new hair casts a soft shadow on the face and neck, and is softened
//    and grained like the photo around it.
//
// Skin is told from hair by its colour (measured on the face itself), not
// only by the parts map, which can call a cheek in shadow hair: the face is
// never cut, and the head is centred on the skin found.
//
// The analysis runs on a working grid (long edge <= 1280) and the hair is
// drawn at most 560 px per face height; both are applied smoothly at the
// cut-out's own size, so a full-resolution save stays light on memory.
// Units for shapes: the face height (hairline to chin) is 1; x from the
// face's centre, y from the hairline (down).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

#include "../matte/tools.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

using namespace matte;

namespace {

enum class Kind { Strands, Coils, Ropes, Dome };
enum class RopeKind { Locs, Braids, Twists, Cornrows, Knots };
enum class Extra { None, TopKnot, Puff, Bantu, Ponytail, CoilBun };

struct Style {
  const char* id;
  bool women;
  Kind kind;
  float top;     // volume above the skull
  float side;    // width beyond the skull at the temples
  float len;     // below the chin (negative: short, above the ears)
  float fringe;  // how far bangs come down the forehead
  float part;    // the parting, -1 (left) .. 1 (right)
  Extra extra;
  bool afro;     // a round shape
  bool slick;    // combed back
  float wave, waveLen;  // strands: coherent waves (amplitude, wavelength)
  float curl;           // strands: per-strand curl amplitude
  float gloss;          // 0..1
  RopeKind rope;
  float ropeW;          // rope width
  bool freeform;        // ropes: uneven, thick (rasta)
  bool highTop = false; // ropes: short locs on top, the sides faded to the skin
};

// clang-format off
const std::vector<Style>& styles() {
  static const std::vector<Style> s = {
    // Women
    {"long-straight",  true,  Kind::Strands, 0.03f, 0.04f, 0.95f, 0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.7f,  RopeKind::Locs, 0, false},
    {"silk-press",     true,  Kind::Strands, 0.0f,  0.02f, 1.05f, 0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   1.0f,  RopeKind::Locs, 0, false},
    {"long-waves",     true,  Kind::Strands, 0.05f, 0.09f, 0.9f,  0.0f,  0.25f, Extra::None,     false, false, 0.024f, 0.26f, 0.0f,   0.65f, RopeKind::Locs, 0, false},
    {"beach-waves",    true,  Kind::Strands, 0.06f, 0.11f, 0.6f,  0.0f,  0.4f,  Extra::None,     false, false, 0.03f,  0.2f,  0.004f, 0.45f, RopeKind::Locs, 0, false},
    {"long-curls",     true,  Kind::Strands, 0.1f,  0.16f, 0.8f,  0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.02f,  0.45f, RopeKind::Locs, 0, false},
    {"bob",            true,  Kind::Strands, 0.03f, 0.06f, 0.02f, 0.0f,  0.3f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.75f, RopeKind::Locs, 0, false},
    {"bob-bangs",      true,  Kind::Strands, 0.03f, 0.06f, 0.0f,  0.3f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.75f, RopeKind::Locs, 0, false},
    {"lob",            true,  Kind::Strands, 0.03f, 0.05f, 0.3f,  0.0f,  0.45f, Extra::None,     false, false, 0.012f, 0.3f,  0.0f,   0.7f,  RopeKind::Locs, 0, false},
    {"pixie",          true,  Kind::Strands, 0.06f, 0.02f, -1.0f, 0.16f, 0.5f,  Extra::None,     false, false, 0.0f,   0.0f,  0.003f, 0.55f, RopeKind::Locs, 0, false},
    {"curtain-bangs",  true,  Kind::Strands, 0.04f, 0.06f, 0.7f,  0.2f,  0.0f,  Extra::None,     false, false, 0.01f,  0.3f,  0.0f,   0.65f, RopeKind::Locs, 0, false},
    {"shag",           true,  Kind::Strands, 0.07f, 0.13f, 0.35f, 0.16f, 0.0f,  Extra::None,     false, false, 0.02f,  0.16f, 0.006f, 0.45f, RopeKind::Locs, 0, false},
    {"side-swept",     true,  Kind::Strands, 0.04f, 0.05f, 0.6f,  0.22f, 0.8f,  Extra::None,     false, false, 0.008f, 0.3f,  0.0f,   0.7f,  RopeKind::Locs, 0, false},
    {"top-knot",       true,  Kind::Strands, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  Extra::TopKnot,  false, true,  0.0f,   0.0f,  0.0f,   0.85f, RopeKind::Locs, 0, false},
    {"sleek-ponytail", true,  Kind::Strands, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  Extra::Ponytail, false, true,  0.0f,   0.0f,  0.0f,   0.9f,  RopeKind::Locs, 0, false},
    {"curly-updo",     true,  Kind::Strands, 0.02f, 0.0f,  -1.0f, 0.0f,  0.0f,  Extra::CoilBun,  false, true,  0.0f,   0.0f,  0.0f,   0.6f,  RopeKind::Locs, 0, false},
    {"afro",           true,  Kind::Coils,   0.2f,  0.3f,  0.0f,  0.0f,  0.0f,  Extra::None,     true,  false, 0.0f,   0.0f,  0.0f,   0.3f,  RopeKind::Locs, 0, false},
    {"afro-puff",      true,  Kind::Strands, 0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  Extra::Puff,     false, true,  0.0f,   0.0f,  0.0f,   0.7f,  RopeKind::Locs, 0, false},
    {"twist-out",      true,  Kind::Coils,   0.14f, 0.22f, 0.2f,  0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.35f, RopeKind::Locs, 0, false},
    {"bantu-knots",    true,  Kind::Ropes,   0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  Extra::Bantu,    false, true,  0.0f,   0.0f,  0.0f,   0.6f,  RopeKind::Knots, 0.035f, false},
    {"box-braids",     true,  Kind::Ropes,   0.03f, 0.06f, 1.0f,  0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.5f,  RopeKind::Braids, 0.045f, false},
    {"cornrow-braids", true,  Kind::Ropes,   0.0f,  0.02f, 0.8f,  0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.5f,  RopeKind::Cornrows, 0.04f, false},
    {"long-locs",      true,  Kind::Ropes,   0.06f, 0.08f, 0.9f,  0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.3f,  RopeKind::Locs, 0.06f, false},
    {"finger-waves",   true,  Kind::Strands, 0.0f,  0.0f,  -1.0f, 0.0f,  0.6f,  Extra::None,     false, true,  0.035f, 0.12f, 0.0f,   0.95f, RopeKind::Locs, 0, false},
    // Men
    {"dreadlocks",     false, Kind::Ropes,   0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.35f, RopeKind::Locs, 0.055f, false, true},
    {"rasta",          false, Kind::Ropes,   0.12f, 0.12f, 1.15f, 0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.25f, RopeKind::Locs, 0.095f, true},
    {"afro-men",       false, Kind::Coils,   0.12f, 0.16f, -0.3f, 0.0f,  0.0f,  Extra::None,     true,  false, 0.0f,   0.0f,  0.0f,   0.3f,  RopeKind::Locs, 0, false},
    {"bald",           false, Kind::Dome,    0.0f,  0.0f,  -1.0f, 0.0f,  0.0f,  Extra::None,     false, false, 0.0f,   0.0f,  0.0f,   0.0f,  RopeKind::Locs, 0, false},
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

// Smooth value noise in -1..1.
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

// Squared colour distance, on a log scale of light (so dark and light
// colours count alike).
inline float dist2(V3 a, V3 b) {
  auto l = [](float v) { return std::log(std::max(v, 1e-3f)); };
  const float dr = l(a.r) - l(b.r), dg = l(a.g) - l(b.g), db = l(a.b) - l(b.b);
  return dr * dr + dg * dg + db * db;
}

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

struct V2 {
  float x = 0, y = 0;
};
inline V2 norm2(V2 v) {
  const float l = std::sqrt(v.x * v.x + v.y * v.y);
  return l > 1e-6f ? V2{v.x / l, v.y / l} : V2{0.0f, 1.0f};
}
inline V3 norm3(V3 v) {
  const float l = std::sqrt(v.r * v.r + v.g * v.g + v.b * v.b);
  return l > 1e-6f ? V3{v.r / l, v.g / l, v.b / l} : V3{0, 0, 1};
}
inline float dot3(V3 a, V3 b) { return a.r * b.r + a.g * b.g + a.b * b.b; }

// A plane on a grid (row-major), sampled bilinearly.
struct Grid {
  int32_t w = 0, h = 0;
  std::vector<float> v;
  void init(int32_t W, int32_t H, float fill = 0.0f) { w = W, h = H, v.assign(static_cast<size_t>(W) * H, fill); }
  float& at(int32_t x, int32_t y) { return v[static_cast<size_t>(y) * w + x]; }
  float at(int32_t x, int32_t y) const { return v[static_cast<size_t>(y) * w + x]; }
  float sample(float x, float y) const {  // pixel-centre coordinates
    const float fx = std::clamp(x - 0.5f, 0.0f, static_cast<float>(w - 1)), fy = std::clamp(y - 0.5f, 0.0f, static_cast<float>(h - 1));
    const auto x0 = static_cast<int32_t>(fx), y0 = static_cast<int32_t>(fy);
    const int32_t x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    return (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
  }
};

void blurGrid(Grid& g, float sigma) {
  if (sigma < 0.3f) return;
  const int r = std::max(1, static_cast<int>(std::ceil(sigma * 2.5f)));
  std::vector<float> k(static_cast<size_t>(2 * r + 1));
  float s = 0;
  for (int i = -r; i <= r; ++i) s += k[static_cast<size_t>(i + r)] = std::exp(-0.5f * static_cast<float>(i * i) / (sigma * sigma));
  for (float& v : k) v /= s;
  std::vector<float> tmp(g.v.size());
  for (int32_t y = 0; y < g.h; ++y)
    for (int32_t x = 0; x < g.w; ++x) {
      float a = 0;
      for (int i = -r; i <= r; ++i) a += k[static_cast<size_t>(i + r)] * g.at(std::clamp(x + i, 0, g.w - 1), y);
      tmp[static_cast<size_t>(y) * g.w + x] = a;
    }
  for (int32_t y = 0; y < g.h; ++y)
    for (int32_t x = 0; x < g.w; ++x) {
      float a = 0;
      for (int i = -r; i <= r; ++i) a += k[static_cast<size_t>(i + r)] * tmp[static_cast<size_t>(std::clamp(y + i, 0, g.h - 1)) * g.w + x];
      g.at(x, y) = a;
    }
}

// The parts map's zone as a soft plane on a grid.
Grid zoneGrid(const ImageBuffer& zones, Zone zone, int32_t W, int32_t H) {
  const int32_t zw = zones.width(), zh = zones.height(), bpp = bytesPerPixel(zones.format());
  const auto id = static_cast<uint8_t>(zone);
  Grid g;
  g.init(W, H);
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
      g.at(x, y) = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
    }
  }
  return g;
}

// Push-pull fill: unknown pixels (w = 0) take the colour of known ones around them.
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
      // The coarser level, enlarged smoothly (bilinear), so fills have no blocks.
      const float fx = std::clamp((static_cast<float>(x) + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<float>(w2 - 1));
      const float fy = std::clamp((static_cast<float>(y) + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<float>(h2 - 1));
      const auto x0 = static_cast<int32_t>(fx), y0 = static_cast<int32_t>(fy);
      const int32_t x1 = std::min(w2 - 1, x0 + 1), y1 = std::min(h2 - 1, y0 + 1);
      const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
      auto C = [&](int32_t xx, int32_t yy) { return c2[static_cast<size_t>(yy) * w2 + xx]; };
      const V3 coarse = (C(x0, y0) * (1 - tx) + C(x1, y0) * tx) * (1 - ty) + (C(x0, y1) * (1 - tx) + C(x1, y1) * tx) * ty;
      c[i] = w[i] > 0.0f ? mix(coarse, c[i], w[i]) : coarse;
    }
}

struct Geometry {
  float cx, top, unit;  // face centre x and hairline y (working-grid pixels); one face height in pixels
  float halfFace;       // half the face width (face heights)
  float skullTop;       // top of the skull (face heights, negative)
  float skullHalf;      // skull half width at the temples
};

// A head without hair: round over the top, its sides coming straight down
// past the temples to the ears (where the face's own sides take over).
float scalpSd(const Geometry& g, float X, float Y) {
  const float cy = 0.2f;
  const float top = ellipseSd(X, Y, 0.0f, cy, g.skullHalf, cy - g.skullTop);
  const float sides = std::max(std::fabs(X) - g.skullHalf * (1.0f - 0.06f * smooth(cy, 0.5f, Y)), std::max(cy - Y, Y - 0.5f));
  return Y < cy ? top : std::min(top, sides);
}

// ---------------------------------------------------------------------------
// The style's shape: signed distance (< 0 inside), whether in an extra (a
// bun, a puff), drawn as coils.
struct ShapeAt {
  float d = 1e9f;
  bool extra = false;
  float dExtra = 1e9f;
};

ShapeAt shapeAt(const Style& st, const Geometry& g, float X, float Y) {
  ShapeAt out;
  const float a = g.halfFace;
  const float skullCy = 0.2f, skullRx = g.skullHalf, skullRy = skullCy - g.skullTop;
  const bool longHair = st.len > -0.5f;
  float d;
  if (st.afro) {
    d = ellipseSd(X, Y, 0.0f, 0.12f - 0.3f * st.top, skullRx + 0.14f + 0.7f * st.side, 0.58f + st.top);
    d += 0.03f * fbm(X * 7.0f, Y * 7.0f, 77);  // a lumpy, natural outline
    d = std::max(d, (Y - (0.8f + std::max(0.0f, st.len))) * 1.5f);
  } else if (st.kind == Kind::Dome || st.highTop) {
    d = scalpSd(g, X, Y);
    (void)a;
  } else {
    const float capRx = skullRx + st.side + 0.012f, capRy = skullRy + 0.5f * st.top + 0.012f, capCy = skullCy - 0.5f * st.top;
    float cap = ellipseSd(X, Y, 0.0f, capCy, capRx, capRy);
    const float earLine = 0.38f;
    if (!longHair) {
      cap = std::max(cap, Y - earLine);
      cap = std::max(cap, std::fabs(X) - (capRx + 0.01f - (0.2f * a) * smooth(0.02f, earLine, Y)));
    }
    d = cap;
    if (longHair) {
      const float bottom = 0.95f + st.len;
      const float flare = (0.1f + 0.12f * smooth(0.6f, bottom, Y)) * smooth(0.1f, 0.9f, Y) + st.side;
      d = std::min(d, boxSd(X, Y, -(skullRx + flare), 0.1f, skullRx + flare, bottom, 0.14f));
    }
  }
  float dx = 1e9f;
  switch (st.extra) {
    case Extra::TopKnot: dx = ellipseSd(X, Y, 0.0f, g.skullTop - 0.12f, 0.2f, 0.17f); break;
    case Extra::CoilBun: dx = ellipseSd(X, Y, 0.0f, g.skullTop - 0.12f, 0.22f, 0.19f); break;
    case Extra::Puff: dx = ellipseSd(X, Y, 0.0f, g.skullTop - 0.12f, 0.36f, 0.3f); break;
    case Extra::Ponytail: dx = ellipseSd(X, Y, skullRx * 0.95f, 0.05f, 0.1f, 0.3f); break;
    case Extra::Bantu: {
      const float pts[5][2] = {{-0.3f, 0.12f}, {0.0f, 0.0f}, {0.3f, 0.12f}, {-0.42f, 0.4f}, {0.42f, 0.4f}};
      for (const auto& p : pts) dx = std::min(dx, ellipseSd(X, Y, p[0] * skullRx / 0.46f, g.skullTop + p[1] * 0.6f, 0.11f, 0.11f));
      break;
    }
    default: break;
  }
  out.dExtra = dx;
  out.extra = dx < 0.0f;
  out.d = std::min(d, dx);
  return out;
}

// The hair's own direction at a point (unit, image space, y down).
V2 flowAt(const Style& st, const Geometry& g, float X, float Y) {
  const float a = g.halfFace;
  const V2 crown{st.part * a * 0.8f, g.skullTop + 0.04f};
  if (st.extra == Extra::TopKnot || st.extra == Extra::CoilBun || st.extra == Extra::Puff) {
    const V2 c{0.0f, g.skullTop - 0.12f};
    const float dx = X - c.x, dy = Y - c.y;
    if (dx * dx / 0.05f + dy * dy / 0.04f < 1.0f) return norm2(V2{-dy, dx});  // around the bun
  }
  if (st.extra == Extra::Ponytail && X > g.skullHalf * 0.8f) return V2{0.15f, 1.0f};
  V2 r = norm2(V2{X - crown.x, Y - crown.y});
  if (st.slick) return V2{-r.x, -r.y};  // combed toward the crown
  if (st.len > -0.5f) {
    const float t = smooth(0.0f, 0.5f, Y);
    const V2 down = norm2(V2{0.12f * (X > 0 ? 1.0f : -1.0f), 1.0f});
    r = norm2(V2{r.x + (down.x - r.x) * t, r.y + (down.y - r.y) * t});
  }
  return r;
}

// Light from the upper left, toward the viewer.
const V3 kLight = norm3(V3{-0.35f, -0.8f, 0.5f});
const V3 kHalf = norm3(V3{-0.35f, -0.8f, 1.5f});

// Hair fibre lighting (Kajiya-Kay): `t` the strand's direction on the
// surface with normal `n`.
V3 litFibre(V3 base, V2 t2, V3 n, float gloss, float shade) {
  V3 t{t2.x, t2.y, -(n.r * t2.x + n.g * t2.y) / std::max(n.b, 0.25f)};
  t = norm3(t);
  const float tl = dot3(t, kLight);
  const float diff = std::sqrt(std::max(0.0f, 1.0f - tl * tl)) * (0.5f + 0.5f * std::max(0.0f, dot3(n, kLight)));
  const V3 t1 = norm3(t + n * 0.1f), t2b = norm3(t + n * -0.15f);
  const float h1 = dot3(t1, kHalf), h2 = dot3(t2b, kHalf);
  const float s1 = std::pow(std::max(0.0f, 1.0f - h1 * h1), 120.0f);
  const float s2 = std::pow(std::max(0.0f, 1.0f - h2 * h2), 30.0f);
  return base * (shade * (0.22f + 0.85f * diff)) + grey(0.045f * gloss * s1 * shade) + base * (1.6f * 0.22f * gloss * s2 * shade);
}

// The hair layer: premultiplied linear colour and coverage on its own grid.
struct Layer {
  int32_t w = 0, h = 0;
  float x0 = 0, y0 = 0, scale = 1;  // face units of pixel (0,0)'s corner; pixels per face height
  std::vector<V3> c;
  std::vector<float> a;
  float toX(float px) const { return x0 + px / scale; }
  float toY(float py) const { return y0 + py / scale; }
  // Paint a soft dot of colour `col` and coverage `alpha` (over).
  void dot(float px, float py, float radius, V3 col, float alpha, const Grid* block) {
    const int32_t xa = static_cast<int32_t>(std::floor(px - radius - 1)), xb = static_cast<int32_t>(std::ceil(px + radius + 1));
    const int32_t ya = static_cast<int32_t>(std::floor(py - radius - 1)), yb = static_cast<int32_t>(std::ceil(py + radius + 1));
    for (int32_t y = std::max(0, ya); y <= std::min(h - 1, yb); ++y)
      for (int32_t x = std::max(0, xa); x <= std::min(w - 1, xb); ++x) {
        const float dx = static_cast<float>(x) + 0.5f - px, dy = static_cast<float>(y) + 0.5f - py;
        float k = std::clamp(radius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f) * alpha;
        if (block != nullptr) k *= block->at(x, y);
        if (k <= 0.0f) continue;
        const size_t i = static_cast<size_t>(y) * w + x;
        c[i] = col * k + c[i] * (1.0f - k);
        a[i] = k + a[i] * (1.0f - k);
      }
  }
};

}  // namespace

const std::vector<HairStyleInfo>& hairStyles() {
  static const std::vector<HairStyleInfo> list = [] {
    std::vector<HairStyleInfo> v;
    for (const auto& s : styles()) v.push_back({s.id, s.women});
    return v;
  }();
  return list;
}

// Hair in a new colour, each strand keeping its light relative to the hair's
// average (`meanLogL`: the mean log lightness of the hair). Shadows go deeper
// and keep some of the natural colour (dye shows least there); highlights
// stay soft and a little lighter. `mix01`: how fully the colour replaces it.
static V3 recolourHair(V3 c, float meanLogL, V3 colour, float mix01) {
  const float lt = std::max(lum(colour), 1e-4f);
  const V3 chroma = colour * (1.0f / lt);
  const float rel = std::exp(std::clamp(std::log(std::max(lum(c), 1e-4f)) - meanLogL, -2.5f, 2.5f));
  float shown = std::pow(rel, lt > 0.15f ? 1.45f : 1.15f);
  if (shown > 1.0f) shown = 1.0f + (shown - 1.0f) / (1.0f + 1.2f * (shown - 1.0f));
  const float L = 0.62f * lt * shown;
  const float shadow = 1.0f - smooth(0.25f, 0.9f, rel), high = smooth(1.2f, 2.5f, rel);
  V3 hue = mix(chroma, V3{std::pow(chroma.r, 1.25f), std::pow(chroma.g, 1.25f), std::pow(chroma.b, 1.25f)}, 0.6f * shadow);
  hue = hue * (1.0f / std::max(lum(hue), 1e-4f));
  hue = mix(hue, mix(hue, grey(1.0f), 0.15f), high);
  V3 outc = hue * L;
  outc = mix(outc, c * (L / std::max(lum(c), 1e-4f)), 0.25f * shadow);
  const float mx = std::max({outc.r, outc.g, outc.b});
  if (mx > 1.0f) outc = outc * (1.0f / mx);
  return mix(c, outc, mix01);
}

Error restyleHair(ImageBuffer& cutout, const ImageBuffer& zones, const HairParams& params, const CancellationToken* token) {
  ScopedTimer timer("studio.hair");
  if (!isColour(cutout)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Hair needs an RGBA cut-out");
  if (zones.width() < 2 || zones.height() < 2) return makeError(PP_ERROR_INVALID_ARGUMENT, "Hair needs the parts map");
  const Style* st = findStyle(params.style);
  if (params.style != nullptr && params.style[0] != '\0' && st == nullptr) return makeError(PP_ERROR_INVALID_ARGUMENT, "Unknown hairstyle");
  if (st == nullptr && !params.recolor) return ok();
  if (params.layer != nullptr && (!isColour(*params.layer) || !(params.layerFace[2] - params.layerFace[0] > 4.0f && params.layerFace[3] - params.layerFace[1] > 4.0f))) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "A hairstyle photo needs RGBA pixels and its face box");
  }
  if (st != nullptr && !(params.faceRight - params.faceLeft > 0.01f && params.faceBottom - params.faceTop > 0.01f)) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "A hairstyle needs a face in the photo");
  }
  const int32_t W = cutout.width(), H = cutout.height();
  const Rgb o = offsets(cutout);
  const auto& Lut = linearLut();

  // ---- the working grid: the cut-out reduced (area average), long edge <= 1280.
  const int32_t f = std::max(1, static_cast<int32_t>(std::ceil(static_cast<float>(std::max(W, H)) / 1280.0f)));
  const int32_t w = (W + f - 1) / f, h = (H + f - 1) / f;
  const size_t n = static_cast<size_t>(w) * h;
  std::vector<V3> col(n);
  std::vector<float> alpha(n, 0.0f);
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      V3 s{};
      float sa = 0, cnt = 0;
      for (int32_t yy = y * f; yy < std::min(H, (y + 1) * f); ++yy)
        for (int32_t xx = x * f; xx < std::min(W, (x + 1) * f); ++xx) {
          const uint8_t* q = cutout.row(yy) + xx * 4;
          const float a = static_cast<float>(q[3]) / 255.0f;
          s = s + V3{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]} * a, sa += a, cnt += 1;
        }
      const size_t i = static_cast<size_t>(y) * w + x;
      col[i] = sa > 0 ? s * (1.0f / sa) : V3{};
      alpha[i] = cnt > 0 ? sa / cnt : 0.0f;
    }
  Grid hairZ = zoneGrid(zones, Zone::Hair, w, h), faceZ = zoneGrid(zones, Zone::FaceSkin, w, h);
  {
    Grid wide = hairZ;
    blurGrid(wide, std::max(1.0f, 0.006f * static_cast<float>(std::min(w, h))));
    for (size_t i = 0; i < n; ++i) hairZ.v[i] = std::max(hairZ.v[i], smooth(0.08f, 0.4f, wide.v[i]));
  }

  // ---- the person's own hair and skin colours.
  double hr = 0, hg = 0, hb = 0, hl = 0, hn = 0;
  std::array<double, 3> sm{}, ss{};
  double sn = 0;
  V3 skinSum{};
  auto feat = [](V3 c) {
    const float s = std::max(c.r + c.g + c.b, 1e-4f);
    return std::array<float, 3>{std::log(std::max(lum(c), 1e-4f)), c.r / s, c.g / s};
  };
  for (size_t i = 0; i < n; ++i) {
    if (alpha[i] < 0.9f) continue;
    if (hairZ.v[i] > 0.8f && faceZ.v[i] < 0.2f) hr += col[i].r, hg += col[i].g, hb += col[i].b, hl += std::log(std::max(lum(col[i]), 1e-4f)), hn += 1;
    if (faceZ.v[i] > 0.9f) {
      const auto fv = feat(col[i]);
      for (int k = 0; k < 3; ++k) sm[static_cast<size_t>(k)] += fv[static_cast<size_t>(k)], ss[static_cast<size_t>(k)] += fv[static_cast<size_t>(k)] * fv[static_cast<size_t>(k)];
      skinSum = skinSum + col[i], sn += 1;
    }
  }
  const V3 ownHair = hn > 30 ? V3{static_cast<float>(hr / hn), static_cast<float>(hg / hn), static_cast<float>(hb / hn)} : V3{0.03f, 0.02f, 0.015f};
  const float ownLogL = hn > 30 ? static_cast<float>(hl / hn) : std::log(0.02f);
  const V3 skin = sn > 30 ? skinSum * (1.0f / static_cast<float>(sn)) : V3{0.25f, 0.15f, 0.1f};
  std::array<float, 3> fm{}, fsd{};
  const std::array<float, 3> floorSd{0.4f, 0.012f, 0.008f};  // light varies across a face (one side in shadow); its colour much less
  for (int k = 0; k < 3; ++k) {
    const auto K = static_cast<size_t>(k);
    fm[K] = sn > 30 ? static_cast<float>(sm[K] / sn) : 0.0f;
    fsd[K] = sn > 30 ? std::max(floorSd[K], static_cast<float>(std::sqrt(std::max(0.0, ss[K] / sn - (sm[K] / sn) * (sm[K] / sn))))) : 1.0f;
  }
  auto skinLike = [&](V3 c) {
    if (sn <= 30) return 0.0f;
    const auto fv = feat(c);
    float d2 = 0;
    for (int k = 0; k < 3; ++k) {
      const auto K = static_cast<size_t>(k);
      const float z = (fv[K] - fm[K]) / (2.5f * fsd[K]);
      d2 += z * z;
    }
    return 1.0f - smooth(1.0f, 2.2f, d2);
  };

  // ---- the face, found by its colour from the parts map's face outward
  // (a cheek in shadow that the parts map calls hair is still face).
  Geometry g{};
  g.cx = 0.5f * (params.faceLeft + params.faceRight) * static_cast<float>(w);
  g.top = params.faceTop * static_cast<float>(h);
  g.unit = std::max(4.0f, (params.faceBottom - params.faceTop) * static_cast<float>(h));
  g.halfFace = std::max(0.2f, 0.5f * (params.faceRight - params.faceLeft) * static_cast<float>(w) / g.unit);
  const float chinY = params.faceBottom * static_cast<float>(h);
  Grid faceKeep;
  faceKeep.init(w, h);
  {
    // Skin is smooth; hair, even hair the colour of skin (lit gold by the
    // sun), is textured: the local spread of lightness tells them apart.
    Grid m1, m2;
    m1.init(w, h), m2.init(w, h);
    for (size_t i = 0; i < n; ++i) {
      const float l = std::log(std::max(lum(col[i]), 1e-4f));
      m1.v[i] = l, m2.v[i] = l * l;
    }
    blurGrid(m1, std::max(1.0f, 0.008f * g.unit)), blurGrid(m2, std::max(1.0f, 0.008f * g.unit));
    std::vector<float> smoothSkin(n);
    for (size_t i = 0; i < n; ++i) smoothSkin[i] = 1.0f - smooth(0.12f, 0.22f, std::sqrt(std::max(0.0f, m2.v[i] - m1.v[i] * m1.v[i])));
    std::vector<uint8_t> seen(n, 0);
    std::vector<int32_t> stack;
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        if (faceZ.v[i] > 0.9f && alpha[i] > 0.5f && skinLike(col[i]) > 0.5f && smoothSkin[i] > 0.5f) seen[i] = 1, stack.push_back(static_cast<int32_t>(i));
      }
    // Only within a face-shaped oval around the parts map's face.
    const float ocx = g.cx, ocy = g.top + 0.5f * g.unit, orx = 1.25f * g.halfFace * g.unit, ory = 0.68f * g.unit;
    while (!stack.empty()) {
      const int32_t k = stack.back();
      stack.pop_back();
      const int32_t x = k % w, y = k / w;
      const int32_t nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
      for (int j = 0; j < 4; ++j) {
        if (nx[j] < 0 || ny[j] < 0 || nx[j] >= w || ny[j] >= h) continue;
        const size_t i = static_cast<size_t>(ny[j]) * w + nx[j];
        if (seen[i] || alpha[i] < 0.5f) continue;
        const float ex = (static_cast<float>(nx[j]) - ocx) / orx, ey = (static_cast<float>(ny[j]) - ocy) / ory;
        if (ex * ex + ey * ey > 1.0f) continue;
        if (skinLike(col[i]) < 0.5f || smoothSkin[i] < 0.5f) continue;
        seen[i] = 1, stack.push_back(static_cast<int32_t>(i));
      }
    }
    // The skin, and everything it encloses: eyes, brows, nostrils and lips
    // are not skin-coloured but lie inside the face; hair at the hairline
    // reaches the hair outside, so it is not enclosed and goes.
    {
      // First seal the thin lines of the features (the edges of the eyes,
      // the nose, the lips are not smooth): close the skin a little.
      {
        const auto rc = std::max<int32_t>(1, static_cast<int32_t>(0.02f * g.unit));
        std::vector<uint8_t> t(n), u(n);
        auto pass = [&](const std::vector<uint8_t>& in, std::vector<uint8_t>& out2, bool grow, bool horizontal) {
          for (int32_t y = 0; y < h; ++y)
            for (int32_t x = 0; x < w; ++x) {
              uint8_t v = grow ? 0 : 1;
              for (int32_t k = -rc; k <= rc; ++k) {
                const int32_t xx = horizontal ? std::clamp(x + k, 0, w - 1) : x, yy = horizontal ? y : std::clamp(y + k, 0, h - 1);
                const uint8_t q = in[static_cast<size_t>(yy) * w + xx];
                v = grow ? std::max(v, q) : std::min(v, q);
              }
              out2[static_cast<size_t>(y) * w + x] = v;
            }
        };
        pass(seen, t, true, true), pass(t, u, true, false), pass(u, t, false, true), pass(t, u, false, false);
        for (size_t i = 0; i < n; ++i) seen[i] = std::max(seen[i], u[i]);
      }
      std::vector<uint8_t> outside(n, 0);
      std::vector<int32_t> st2;
      for (int32_t x = 0; x < w; ++x)
        for (int32_t y : {0, h - 1}) {
          const size_t i = static_cast<size_t>(y) * w + x;
          if (!seen[i] && !outside[i]) outside[i] = 1, st2.push_back(static_cast<int32_t>(i));
        }
      for (int32_t y = 0; y < h; ++y)
        for (int32_t x : {0, w - 1}) {
          const size_t i = static_cast<size_t>(y) * w + x;
          if (!seen[i] && !outside[i]) outside[i] = 1, st2.push_back(static_cast<int32_t>(i));
        }
      while (!st2.empty()) {
        const int32_t k = st2.back();
        st2.pop_back();
        const int32_t x = k % w, y = k / w;
        const int32_t nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int j = 0; j < 4; ++j) {
          if (nx[j] < 0 || ny[j] < 0 || nx[j] >= w || ny[j] >= h) continue;
          const size_t i = static_cast<size_t>(ny[j]) * w + nx[j];
          if (seen[i] || outside[i]) continue;
          outside[i] = 1, st2.push_back(static_cast<int32_t>(i));
        }
      }
      // Enclosed holes count only inside the face itself (not a pocket of
      // hair between the jaw, the neck and a shoulder).
      for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x) {
          const size_t i = static_cast<size_t>(y) * w + x;
          const float ex = (static_cast<float>(x) - ocx) / (orx * 0.85f), ey = (static_cast<float>(y) - (g.top + 0.45f * g.unit)) / (ory * 0.85f);
          const bool hole = !outside[i] && ex * ex + ey * ey < 1.0f;
          // The parts map's face is reliable below the top of the forehead
          // (eyes, mouth, a beard); only near the hairline can it take hair.
          const bool partsFace = faceZ.v[i] > 0.5f && (static_cast<float>(y) - g.top) / g.unit > 0.14f;
          faceKeep.v[i] = (seen[i] || hole || partsFace) ? 1.0f : 0.0f;
        }
    }
    // If the face's colour couldn't be learnt, the parts map's face stands.
    if (sn <= 30)
      for (size_t i = 0; i < n; ++i) faceKeep.v[i] = smooth(0.5f, 0.9f, faceZ.v[i]);
    blurGrid(faceKeep, std::max(0.7f, 0.004f * g.unit));
    // Re-centre on the skin found at cheek level.
    std::vector<float> ls, rs;
    for (float fy = 0.35f; fy <= 0.62f; fy += 0.03f) {
      const auto y = static_cast<int32_t>(g.top + fy * g.unit);
      const auto c0 = static_cast<int32_t>(g.cx);
      if (y < 0 || y >= h || c0 < 0 || c0 >= w || faceKeep.at(c0, y) < 0.5f) continue;
      int32_t l = c0, r = c0;
      while (l > 0 && faceKeep.at(l - 1, y) > 0.5f) --l;
      while (r < w - 1 && faceKeep.at(r + 1, y) > 0.5f) ++r;
      ls.push_back(static_cast<float>(l)), rs.push_back(static_cast<float>(r + 1));
    }
    if (ls.size() >= 3) {
      std::sort(ls.begin(), ls.end()), std::sort(rs.begin(), rs.end());
      const float l = ls[ls.size() / 2], r = rs[rs.size() / 2];
      const float half = 0.5f * (r - l) / g.unit;
      if (half > g.halfFace * 0.85f && half < g.halfFace * 1.45f) g.cx = 0.5f * (l + r), g.halfFace = half;
    }
  }
  const float newColourMix = params.recolor ? std::clamp(params.strength, 0.0f, 1.0f) : 0.0f;
  const V3 newColour = params.recolor ? V3{Lut[params.color[0]], Lut[params.color[1]], Lut[params.color[2]]} : ownHair;

  if (st == nullptr) {
    // ---- a new colour for the person's own hair.
    Grid m;
    m.init(w, h);
    for (size_t i = 0; i < n; ++i) m.v[i] = hairZ.v[i] * (1.0f - faceKeep.v[i]);
    const float strength = newColourMix;
    const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
      for (int32_t y = b0; y < b1; ++y) {
        uint8_t* p = cutout.row(y);
        for (int32_t x = 0; x < W; ++x) {
          uint8_t* q = p + x * 4;
          if (q[3] == 0) continue;
          const float mm = m.sample((static_cast<float>(x) + 0.5f) / static_cast<float>(f), (static_cast<float>(y) + 0.5f) / static_cast<float>(f)) * strength;
          if (mm <= 0.002f) continue;
          const V3 c{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]};
          const V3 outc = recolourHair(c, ownLogL, newColour, mm);
          q[o.r] = toByte(toGamma(outc.r)), q[o.g] = toByte(toGamma(outc.g)), q[o.b] = toByte(toGamma(outc.b));
        }
      }
    });
    return done ? ok() : cancelledError();
  }

  // ---- the head: its own top and width from the outline, toward an average skull.
  {
    float topY = 1e9f;
    for (auto x = static_cast<int32_t>(g.cx - 0.5f * g.halfFace * g.unit); x <= static_cast<int32_t>(g.cx + 0.5f * g.halfFace * g.unit); ++x) {
      if (x < 0 || x >= w) continue;
      for (int32_t y = 0; y < h; ++y)
        if (alpha[static_cast<size_t>(y) * w + x] > 0.5f) {
          topY = std::min(topY, static_cast<float>(y));
          break;
        }
    }
    // A shaved head sits inside the hair's outline.
    const bool shaved = st->kind == Kind::Dome || st->highTop;
    // A shaved head sits just inside the outline of short hair.
    g.skullTop = std::clamp((topY - g.top) / g.unit + (shaved ? 0.045f : 0.03f), -0.38f, -0.12f);
    const auto row = static_cast<int32_t>(std::clamp(g.top + 0.1f * g.unit, 0.0f, static_cast<float>(h - 1)));
    int32_t l = w, r = -1;
    for (int32_t x = 0; x < w; ++x)
      if (alpha[static_cast<size_t>(row) * w + x] > 0.5f) l = std::min(l, x), r = std::max(r, x);
    const float ownHalf = r > l ? 0.5f * static_cast<float>(r - l) / g.unit : g.halfFace * 1.12f;
    g.skullHalf = std::clamp(ownHalf, g.halfFace * 1.0f, g.halfFace * 1.12f);
    if (shaved) {
      // As wide as the head itself (the outline at the temples, less the
      // hair's thickness): the temples and the sides above the ears were
      // under the hair, and are head too.
      g.skullHalf = std::clamp(ownHalf - 0.035f, g.halfFace * 0.95f, g.halfFace * 1.18f);
    }
  }
  const float a = g.halfFace;
  auto hairline = [&](float X) {
    const float u = X / std::max(a, 1e-3f);
    return -0.03f + 0.12f * u * u;
  };

  float fitAng = 0.0f;           // the head's tilt (from the face's fitted outline)
  std::vector<V3> skinOut;       // the person's skin, filled smoothly (for a forehead under a new hairline)
  const bool useLayer = params.layer != nullptr && params.layer->width() > 1 && params.layer->height() > 1;
  // ---- 1. Take the person's own hair out (on the working grid: a factor
  // for the alpha, and a colour filled in where hair lay over the body).
  Grid keepA, fillMix;
  keepA.init(w, h, 1.0f), fillMix.init(w, h, 0.0f);
  // Under the hair, where the texture is borrowed from: an offset to a spot
  // of the same garment (or skin) that shows, in working-grid pixels.
  std::vector<int8_t> klab;
  std::vector<uint8_t> fillFace(n, 0), hasOff(n, 0);
  std::vector<int16_t> offX(n, 0), offY(n, 0);
  std::vector<V3> fill = col;
  {
    const Grid body = zoneGrid(zones, Zone::BodySkin, w, h), cloth = zoneGrid(zones, Zone::Clothes, w, h), acc = zoneGrid(zones, Zone::Accessory, w, h);
    Grid keep;
    keep.init(w, h);
    for (size_t i = 0; i < n; ++i) keep.v[i] = std::max({faceKeep.v[i], body.v[i], cloth.v[i], acc.v[i]});
    Grid kb = keep;
    blurGrid(kb, std::max(1.0f, 0.004f * static_cast<float>(std::min(w, h))));
    for (size_t i = 0; i < n; ++i) keep.v[i] = std::max(keep.v[i], smooth(0.2f, 0.6f, kb.v[i]));
    std::vector<float> known(n);
    for (size_t i = 0; i < n; ++i) known[i] = (alpha[i] > 0.9f && hairZ.v[i] < 0.05f) ? 1.0f : 0.0f;
    // ---- The face's true outline. Hair over one side of the face hides its
    // edge, so the outline is fitted (an oval, tilted with the head) to the
    // edges that border the background or the neck, and must hold all the
    // visible face; the oval then says where a cheek lay under the hair.
    float ecx = g.cx, ecy = g.top + 0.5f * g.unit, erx = g.halfFace * g.unit, ery = 0.62f * g.unit, eang = 0.0f;
    {
      std::vector<std::array<float, 2>> sure, edge;
      for (int32_t y = 1; y < h - 1; ++y)
        for (int32_t x = 1; x < w - 1; ++x) {
          const size_t i = static_cast<size_t>(y) * w + x;
          if (faceKeep.v[i] < 0.5f) continue;
          bool border = false, bg = false, hairSide = false;
          const size_t nb[4] = {i - 1, i + 1, i - static_cast<size_t>(w), i + static_cast<size_t>(w)};
          for (size_t j : nb) {
            if (faceKeep.v[j] >= 0.5f) continue;
            border = true;
            if (alpha[j] < 0.5f) bg = true;
            else if (hairZ.v[j] > 0.3f) hairSide = true;
          }
          if (!border) continue;
          const auto pt = std::array<float, 2>{static_cast<float>(x), static_cast<float>(y)};
          edge.push_back(pt);
          // The chin against the neck, or the face against the background.
          const bool chin = !hairSide && (static_cast<float>(y) - g.top) / g.unit > 0.75f;
          if (bg || chin) sure.push_back(pt);
        }
      auto thin = [](std::vector<std::array<float, 2>>& v, size_t most) {
        if (v.size() <= most) return;
        std::vector<std::array<float, 2>> kept;
        const double step = static_cast<double>(v.size()) / static_cast<double>(most);
        for (size_t k = 0; k < most; ++k) kept.push_back(v[static_cast<size_t>(static_cast<double>(k) * step)]);
        v.swap(kept);
      };
      thin(sure, 240), thin(edge, 240);
      if (sure.size() >= 12 && edge.size() >= 12) {
        const float U = g.unit, hf = g.halfFace * g.unit;
        float best = 1e9f;
        for (float an = -0.44f; an <= 0.441f; an += 0.087f) {
          const float ca = std::cos(an), sa = std::sin(an);
          for (float dx = -0.3f; dx <= 0.301f; dx += 0.03f)
            for (float dy = -0.1f; dy <= 0.101f; dy += 0.025f)
              for (float kx = 0.9f; kx <= 1.341f; kx += 0.04f)
                for (float ky = 0.5f; ky <= 0.601f; ky += 0.025f) {
                  const float cx = g.cx + dx * U, cy = g.top + (0.5f + dy) * U, rx = kx * hf, ry = ky * U;
                  auto r = [&](const std::array<float, 2>& q) {
                    const float px = q[0] - cx, py = q[1] - cy;
                    const float u = px * ca + py * sa, v = -px * sa + py * ca;
                    return std::sqrt(u * u / (rx * rx) + v * v / (ry * ry));
                  };
                  float fit = 0, out = 0;
                  for (const auto& q : sure) fit += std::abs(r(q) - 1.0f);
                  for (const auto& q : edge) out += std::max(0.0f, r(q) - 1.0f);
                  const float score = fit / static_cast<float>(sure.size()) + 3.0f * out / static_cast<float>(edge.size()) + 0.25f * std::max(0.0f, kx - 1.0f) +
                                      0.01f * std::abs(an);
                  if (score < best) best = score, ecx = cx, ecy = cy, erx = rx, ery = ry, eang = an;
                }
        }
      }
    }
    const float eca = std::cos(eang), esa = std::sin(eang);
    fitAng = eang;
    auto inOval = [&](int32_t x, int32_t y) {
      const float px = static_cast<float>(x) - ecx, py = static_cast<float>(y) - ecy;
      const float u = px * eca + py * esa, v = -px * esa + py * eca;
      return u * u / (erx * erx) + v * v / (ery * ery) < 1.0f;
    };

    // ---- The body under long hair: per row, the body's outline. Where its
    // edge meets the background (or the photo's border) it is seen; where it
    // meets hair the hair hid it, and the edge is drawn between the neck and
    // the next seen edge below (a shoulder's curve), or mirrored from the other
    // side; never narrower than the body seen, never past the person's outline.
    std::vector<float> spanL(static_cast<size_t>(h), 0.0f), spanR(static_cast<size_t>(h), -1.0f);
    {
      const auto y0 = static_cast<int32_t>(std::clamp(chinY, 0.0f, static_cast<float>(h - 1)));
      const float neckHalf = 0.55f * g.halfFace * g.unit;
      const auto H2 = static_cast<size_t>(h);
      std::vector<float> E[2] = {std::vector<float>(H2, -1.0f), std::vector<float>(H2, -1.0f)};
      std::vector<float> A[2] = {std::vector<float>(H2, -1.0f), std::vector<float>(H2, -1.0f)};
      std::vector<uint8_t> rel[2] = {std::vector<uint8_t>(H2, 0), std::vector<uint8_t>(H2, 0)};
      auto vis = [&](int32_t x, int32_t y) {
        const size_t i = static_cast<size_t>(y) * w + x;
        return alpha[i] > 0.5f && hairZ.v[i] < 0.3f;
      };
      for (int32_t y = y0; y < h; ++y) {
        for (int side = 0; side < 2; ++side) {
          const int32_t dir = side == 0 ? 1 : -1, start = side == 0 ? 0 : w - 1;
          // The person's outline, and the first run of 3 visible body pixels.
          int32_t ax = -1, ex = -1;
          for (int32_t x = start; x >= 0 && x < w; x += dir) {
            if (ax < 0 && alpha[static_cast<size_t>(y) * w + x] > 0.5f) ax = x;
            const int32_t x2 = x + 2 * dir;
            if (x2 >= 0 && x2 < w && vis(x, y) && vis(x + dir, y) && vis(x2, y)) {
              ex = x;
              break;
            }
          }
          if (ax < 0) continue;
          A[side][static_cast<size_t>(y)] = static_cast<float>(ax);
          if (ex < 0) continue;
          E[side][static_cast<size_t>(y)] = static_cast<float>(ex);
          bool hidden = false;
          for (int32_t k = 1; k <= 3; ++k) {
            const int32_t xx = ex - k * dir;
            if (xx >= 0 && xx < w && alpha[static_cast<size_t>(y) * w + xx] > 0.5f) hidden = true;
          }
          rel[side][static_cast<size_t>(y)] = hidden ? 0 : 1;
        }
      }
      std::vector<float> est[2] = {std::vector<float>(H2, -1.0f), std::vector<float>(H2, -1.0f)};
      for (int side = 0; side < 2; ++side) {
        const float sgn = side == 0 ? -1.0f : 1.0f;  // outward
        auto& e = E[side];
        auto& r = rel[side];
        // The neck under the chin.
        const float neck = g.cx + sgn * neckHalf;
        const auto Y0 = static_cast<size_t>(y0);
        if (!r[Y0]) e[Y0] = e[Y0] < 0 ? neck : (side == 0 ? std::min(e[Y0], neck) : std::max(e[Y0], neck)), r[Y0] = 1;
        size_t last = Y0;
        for (size_t y = Y0; y < H2; ++y) {
          if (r[y] && e[y] >= 0) {
            est[side][y] = e[y], last = y;
            continue;
          }
          size_t next = y + 1;
          while (next < H2 && !(r[next] && e[next] >= 0)) ++next;
          float v;
          if (next < H2) {
            const float t = static_cast<float>(y - last) / static_cast<float>(next - last);
            v = e[last] + (e[next] - e[last]) * t * t * (3.0f - 2.0f * t);
          } else {
            const auto& other = E[1 - side];
            const bool otherSeen = rel[1 - side][y] && other[y] >= 0;
            v = otherSeen ? 2.0f * g.cx - other[y] : e[last];
            // Never narrower than the last seen edge above.
            v = side == 0 ? std::min(v, e[last]) : std::max(v, e[last]);
          }
          est[side][y] = v;
        }
        // A shoulder drawn where hair hid it slopes down from the neck (not
        // a ledge sticking out under the chin).
        for (size_t y = Y0; y < H2; ++y) {
          if (r[y] && e[y] >= 0) continue;
          const float most = neckHalf + 1.6f * static_cast<float>(y - Y0);
          est[side][y] = side == 0 ? std::max(est[side][y], g.cx - most) : std::min(est[side][y], g.cx + most);
        }
        // Inside the person's outline; holding the body seen.
        for (size_t y = Y0; y < H2; ++y) {
          float v = est[side][y];
          if (A[side][y] < 0) {
            est[side][y] = -1;
            continue;
          }
          v = side == 0 ? std::max(v, A[side][y]) : std::min(v, A[side][y]);
          if (e[y] >= 0) v = side == 0 ? std::min(v, e[y]) : std::max(v, e[y]);
          est[side][y] = v;
        }
      }
      // A little smoothing down the rows (no stair steps).
      const auto rr = std::max<int32_t>(1, static_cast<int32_t>(0.015f * g.unit));
      for (int32_t y = y0; y < h; ++y) {
        float sl = 0, sr = 0, cnt = 0;
        for (int32_t k = -rr; k <= rr; ++k) {
          const int32_t yy = y + k;
          if (yy < y0 || yy >= h || est[0][static_cast<size_t>(yy)] < 0 || est[1][static_cast<size_t>(yy)] < 0) continue;
          sl += est[0][static_cast<size_t>(yy)], sr += est[1][static_cast<size_t>(yy)], cnt += 1;
        }
        if (cnt > 0 && est[0][static_cast<size_t>(y)] >= 0) spanL[static_cast<size_t>(y)] = sl / cnt, spanR[static_cast<size_t>(y)] = sr / cnt;
      }
    }
    auto knownAt = [&](int32_t x, int32_t y) {
      if (x < 0 || y < 0 || x >= w || y >= h) return false;
      return known[static_cast<size_t>(y) * w + x] > 0.5f;
    };
    // What lay under the hair: the mirror image (across the face's own axis
    // for a cheek, the body's middle below) when that side shows; else the
    // body just below the hair, reflected upward (the cloth's own texture);
    // else a smooth fill.
    // What lay under the hair. A cheek: the mirror image across the face's
    // own axis when that side shows, else skin. The body: the shading of the
    // nearest part seen (skin or cloth, never a blend of the two), smoothly
    // filled; its fine texture is added at full size from the same garment
    // (or skin) where it shows (below).
    std::vector<V3> skinFill = col, clothFill = col;
    klab.assign(n, -1);
    Grid clothW;
    clothW.init(w, h);
    {
      std::vector<float> ks(n, 0.0f), kc(n, 0.0f);
      std::vector<int8_t> lab(n, -1);
      std::vector<int32_t> queue;
      queue.reserve(n);
      for (size_t i = 0; i < n; ++i) {
        if (known[i] < 0.5f) continue;
        const bool isCloth = std::max(cloth.v[i], acc.v[i]) > std::max(body.v[i], faceKeep.v[i]);
        (isCloth ? kc : ks)[i] = 1.0f;
        lab[i] = isCloth ? 1 : 0;
        klab[i] = lab[i];
        queue.push_back(static_cast<int32_t>(i));
      }
      for (size_t qi = 0; qi < queue.size(); ++qi) {
        const int32_t k = queue[qi];
        const int32_t x = k % w, y = k / w;
        const int32_t nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int j = 0; j < 4; ++j) {
          if (nx[j] < 0 || ny[j] < 0 || nx[j] >= w || ny[j] >= h) continue;
          const size_t i = static_cast<size_t>(ny[j]) * w + nx[j];
          if (lab[i] >= 0) continue;
          lab[i] = lab[static_cast<size_t>(k)], queue.push_back(static_cast<int32_t>(i));
        }
      }
      // The neck is skin. Out past it (the shoulders), what the body nearby
      // shows outside the neck: a sleeve, or bare skin; counted from the
      // pixels seen around it, so no single strip of view decides.
      const float neckBand = 0.6f * g.halfFace * g.unit;
      Grid vs, vc;
      vs.init(w, h), vc.init(w, h);
      double sideS[2] = {0, 0}, sideC[2] = {0, 0};
      for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x) {
          const size_t i = static_cast<size_t>(y) * w + x;
          const float off = static_cast<float>(x) - g.cx;
          if (known[i] < 0.5f || static_cast<float>(y) < chinY || std::abs(off) < neckBand) continue;
          (lab[i] == 1 ? vc : vs).v[i] = 1.0f;
          (lab[i] == 1 ? sideC : sideS)[off < 0 ? 0 : 1] += 1;
        }
      blurGrid(vs, std::max(1.0f, 0.15f * g.unit)), blurGrid(vc, std::max(1.0f, 0.15f * g.unit));
      for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x) {
          const size_t i = static_cast<size_t>(y) * w + x;
          float v = lab[i] == 1 ? 1.0f : 0.0f;
          if (known[i] < 0.5f && static_cast<float>(y) >= chinY) {
            const float off = static_cast<float>(x) - g.cx;
            if (std::abs(off) < neckBand) {
              v = 0.0f;
            } else {
              const float t = vs.v[i] + vc.v[i];
              const int sd2 = off < 0 ? 0 : 1;
              const double all = sideS[sd2] + sideC[sd2];
              v = t > 1e-3f ? vc.v[i] / t : (all > 0 ? static_cast<float>(sideC[sd2] / all) : 1.0f);
            }
          }
          clothW.v[i] = v;
        }
      blurGrid(clothW, std::max(1.0f, 0.02f * g.unit));
      pushPull(skinFill, ks, w, h);
      skinOut = skinFill;
      pushPull(clothFill, kc, w, h);
    }
    auto under = [&](int32_t x, int32_t y, bool face) {
      const size_t i = static_cast<size_t>(y) * w + x;
      if (face) {
        const float px = static_cast<float>(x) - ecx, py = static_cast<float>(y) - ecy;
        const float ax = -esa, ay = eca;  // the face's up-down axis
        const float d = px * ax + py * ay;
        const auto mx = static_cast<int32_t>(std::lround(ecx + 2.0f * d * ax - px)), my = static_cast<int32_t>(std::lround(ecy + 2.0f * d * ay - py));
        if (knownAt(mx, my) && faceKeep.at(mx, my) > 0.5f) return col[static_cast<size_t>(my) * w + mx];
        return skinFill[i];
      }
      return mix(skinFill[i], clothFill[i], clothW.v[i]);
    };
    // Near the hair: faint strands the parts map missed (wisps against the
    // background, not body or clothes) go with it.
    Grid nearHair = hairZ;
    blurGrid(nearHair, std::max(1.0f, 0.1f * g.unit));
    for (int32_t y = 0; y < h; ++y) {
      const bool belowChin = static_cast<float>(y) >= chinY;
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        const float hm = hairZ.v[i] * (1.0f - faceKeep.v[i]);
        if (hm <= 0.0f) {
          if (alpha[i] > 0.0f && alpha[i] < 0.7f && nearHair.v[i] > 0.03f && keep.v[i] < 0.5f) keepA.v[i] = 0.0f;
          continue;
        }
        // A cheek, below the hairline (the top of the head is the new style's).
        bool face = inOval(x, y) && alpha[i] > 0.5f && (static_cast<float>(y) - g.top) / g.unit > 0.18f;
        if (face) {
          // Only as far out as the other side of the face shows (a face is
          // close to symmetric; past that, it is guesswork).
          const float px = static_cast<float>(x) - ecx, py = static_cast<float>(y) - ecy;
          const float d = px * -esa + py * eca;
          const auto mx = static_cast<int32_t>(std::lround(ecx + 2.0f * d * -esa - px)), my = static_cast<int32_t>(std::lround(ecy + 2.0f * d * eca - py));
          // ... and clearly hidden: the mirror point must lie well inside the
          // other side (a sliver of hair at the edge only widens the face).
          const float inX = -px / std::max(std::abs(px), 1e-3f) * 0.05f * g.unit;  // toward the axis, mirrored side
          const auto mx2 = static_cast<int32_t>(std::lround(static_cast<float>(mx) - inX)), my2 = my;
          face = mx >= 0 && my >= 0 && mx < w && my < h && faceKeep.at(mx, my) > 0.5f && mx2 >= 0 && mx2 < w && faceKeep.at(mx2, my2) > 0.5f &&
                 [&] {
                   // the pixel 0.05 face units beyond it, on the mirror side, is face too
                   const auto mx3 = static_cast<int32_t>(std::lround(static_cast<float>(mx) + inX));
                   return mx3 >= 0 && mx3 < w && faceKeep.at(mx3, my) > 0.5f;
                 }();
        }
        const bool onBody = belowChin && alpha[i] > 0.5f && static_cast<float>(x) >= spanL[static_cast<size_t>(y)] && static_cast<float>(x) <= spanR[static_cast<size_t>(y)];
        if (face || onBody) {
          // Hair over a cheek or over the body: what was under it.
          fill[i] = under(x, y, face);
          // Fully replaced, not tinted by the old hair (which reads as a veil).
          fillMix.v[i] = smooth(0.05f, 0.4f, hm);
          fillFace[i] = face ? 1 : 0;
          keepA.v[i] = 1.0f;
        } else {
          // Hair against the background goes, stray strands too.
          const float gone = keep.v[i] * (1.0f - smooth(0.05f, 0.35f, hm));
          keepA.v[i] = gone + (1.0f - gone) * faceKeep.v[i];
        }
      }
    }
    // Scraps of cloth glimpsed between strands, where hair covered most of
    // the body, go with the hair (or they float in the fill as patches).
    {
      Grid dense;
      dense.init(w, h);
      for (size_t i = 0; i < n; ++i) dense.v[i] = fillMix.v[i] > 0.0f ? 1.0f : 0.0f;
      blurGrid(dense, std::max(1.0f, 0.03f * g.unit));
      for (int32_t y = 0; y < h; ++y) {
        if (static_cast<float>(y) < chinY) continue;
        for (int32_t x = 0; x < w; ++x) {
          const size_t i = static_cast<size_t>(y) * w + x;
          if (faceKeep.v[i] > 0.5f || alpha[i] < 0.5f || static_cast<float>(x) < spanL[static_cast<size_t>(y)] || static_cast<float>(x) > spanR[static_cast<size_t>(y)]) continue;
          float k = smooth(0.45f, 0.75f, dense.v[i]);
          // Hair tips the parts map missed: near the hair, closer in colour
          // to the person's hair than to what the body shows here.
          if (nearHair.v[i] > 0.02f && known[i] > 0.5f) {
            const V3 c = col[i], b2 = mix(skinFill[i], clothFill[i], clothW.v[i]);
            const float dh = dist2(c, ownHair), db = dist2(c, b2);
            k = std::max(k, smooth(0.0f, 1.0f, (db - 1.5f * dh) / std::max(db, 1e-5f)));
          }
          if (k <= fillMix.v[i]) continue;
          if (fillMix.v[i] <= 0.0f) fill[i] = under(x, y, false);
          fillMix.v[i] = k;
        }
      }
    }
    // A rebuilt cheek's outer edge is soft, like the face's own edge.
    {
      Grid ff;
      ff.init(w, h);
      for (size_t i = 0; i < n; ++i) ff.v[i] = fillFace[i] ? fillMix.v[i] : 0.0f;
      blurGrid(ff, std::max(0.7f, 0.006f * g.unit));
      for (size_t i = 0; i < n; ++i) {
        if (!fillFace[i] && ff.v[i] > 0.0f && fillMix.v[i] <= 0.0f && hairZ.v[i] > 0.0f) {
          fill[i] = skinFill[i], fillFace[i] = 1;
          fillMix.v[i] = ff.v[i], keepA.v[i] = std::max(keepA.v[i], ff.v[i]);
        } else if (fillFace[i]) {
          fillMix.v[i] = std::min(fillMix.v[i], std::max(ff.v[i], faceKeep.v[i]));
        }
      }
    }
    // Soften the filled colours a little (no hard pixel seams), keeping texture.
    {
      std::vector<V3> soft = fill;
      for (int32_t y = 1; y < h - 1; ++y)
        for (int32_t x = 1; x < w - 1; ++x) {
          const size_t i = static_cast<size_t>(y) * w + x;
          if (fillMix.v[i] <= 0.0f) continue;
          V3 a3{};
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) a3 = a3 + fill[static_cast<size_t>(y + dy) * w + x + dx];
          soft[i] = mix(fill[i], a3 * (1.0f / 9.0f), 0.5f);
        }
      fill.swap(soft);
    }
    // Where each filled pixel borrows its texture: a few large shifts, each
    // copying a whole stretch of the same garment (or skin) where it shows,
    // so the texture stays continuous instead of repeating one small patch.
    // Shifts are chosen greedily: the one covering most of what is left
    // (nearer ones preferred), until nearly all is covered.
    {
      const auto step = std::max<int32_t>(2, static_cast<int32_t>(0.05f * g.unit));
      const auto R = std::max<int32_t>(step * 2, static_cast<int32_t>(1.5f * g.unit));
      std::vector<std::array<int32_t, 2>> cand;
      for (int32_t j = -R / step; j <= R / step; ++j)
        for (int32_t i2 = -R / step; i2 <= R / step; ++i2)
          if ((i2 != 0 || j != 0) && i2 * i2 + j * j <= (R / step) * (R / step)) cand.push_back({i2 * step, j * step});
      const int32_t ring = std::max<int32_t>(2, static_cast<int32_t>(0.015f * g.unit));
      auto solid = [&](int32_t x, int32_t y, int8_t want) {
        for (const auto& d : {std::array<int32_t, 2>{0, 0}, {ring, 0}, {-ring, 0}, {0, ring}, {0, -ring}}) {
          const int32_t xx = x + d[0], yy = y + d[1];
          if (xx < 0 || yy < 0 || xx >= w || yy >= h) return false;
          const size_t j2 = static_cast<size_t>(yy) * w + xx;
          if (klab[j2] != want || fillMix.v[j2] > 0.0f || nearHair.v[j2] > 0.15f) return false;
        }
        return true;
      };
      for (int8_t want = 0; want <= 1; ++want) {
        std::vector<int32_t> todo;
        for (size_t i = 0; i < n; ++i)
          if (fillMix.v[i] > 0.0f && !fillFace[i] && (clothW.v[i] > 0.5f ? 1 : 0) == want) todo.push_back(static_cast<int32_t>(i));
        const size_t total = todo.size();
        for (int round = 0; round < 10 && todo.size() > total / 50 && !todo.empty(); ++round) {
          // Score on a sample of what is left.
          const size_t stride = std::max<size_t>(1, todo.size() / 3000);
          float best = 0.0f;
          std::array<int32_t, 2> pick{0, 0};
          for (const auto& c2 : cand) {
            size_t hit = 0;
            for (size_t t = 0; t < todo.size(); t += stride) {
              const int32_t x = todo[t] % w, y = todo[t] / w;
              if (solid(x + c2[0], y + c2[1], want)) ++hit;
            }
            const float len = std::sqrt(static_cast<float>(c2[0] * c2[0] + c2[1] * c2[1])) / static_cast<float>(R);
            const float score = static_cast<float>(hit) * (1.0f - 0.3f * len);
            if (score > best) best = score, pick = c2;
          }
          if (best <= 0.0f) break;
          std::vector<int32_t> left;
          for (const int32_t k : todo) {
            const int32_t x = k % w, y = k / w;
            if (solid(x + pick[0], y + pick[1], want)) {
              offX[static_cast<size_t>(k)] = static_cast<int16_t>(pick[0]), offY[static_cast<size_t>(k)] = static_cast<int16_t>(pick[1]), hasOff[static_cast<size_t>(k)] = 1;
            } else {
              left.push_back(k);
            }
          }
          todo.swap(left);
        }
      }
    }
  }

  // ---- 2. Draw the style on its own grid.
  // ---- A photographed hairstyle: where it goes on this head. A similarity
  // (its face's width to this face's, chin to chin, tilted with the head),
  // then a radial fit around the face: the hair's inner edge is drawn in (or
  // out) to meet this face's own outline at the forehead and the sides, so no
  // gap and no hair over the cheeks; the farther hair moves with it, smoothly.
  const ImageBuffer* lay = useLayer ? params.layer : nullptr;
  const Rgb lo = useLayer ? offsets(*lay) : Rgb{};
  const float lfw = params.layerFace[2] - params.layerFace[0];
  const float simS = static_cast<float>(params.faceRight - params.faceLeft) * static_cast<float>(w) / std::max(lfw, 1.0f);  // grid px per layer px
  const V2 chinT{0.5f * (params.faceLeft + params.faceRight) * static_cast<float>(w), params.faceBottom * static_cast<float>(h)};
  const V2 chinD{0.5f * (params.layerFace[0] + params.layerFace[2]), params.layerFace[3]};
  const float rca = std::cos(fitAng), rsa = std::sin(fitAng);
  auto toLayer = [&](float gx, float gy) {  // grid point -> layer pixel (inverse similarity)
    const float dx = (gx - chinT.x) / simS, dy = (gy - chinT.y) / simS;
    return V2{chinD.x + dx * rca + dy * rsa, chinD.y - dx * rsa + dy * rca};
  };
  auto toGrid = [&](float lx, float ly) {
    const float dx = (lx - chinD.x) * simS, dy = (ly - chinD.y) * simS;
    return V2{chinT.x + dx * rca - dy * rsa, chinT.y + dx * rsa + dy * rca};
  };
  // The layer's colour and cover at a layer point (bilinear, premultiplied).
  auto layerAt = [&](float lx, float ly, V3* colour) {
    const float fx = lx - 0.5f, fy = ly - 0.5f;
    const auto x0 = static_cast<int32_t>(std::floor(fx)), y0 = static_cast<int32_t>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    V3 c{};
    float cover = 0.0f;
    for (int k = 0; k < 4; ++k) {
      const int32_t xx = x0 + (k & 1), yy = y0 + (k >> 1);
      if (xx < 0 || yy < 0 || xx >= lay->width() || yy >= lay->height()) continue;
      const float wk = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? ty : 1.0f - ty);
      const uint8_t* q = lay->row(yy) + xx * 4;
      const float qa = static_cast<float>(q[3]) / 255.0f * wk;
      c = c + V3{Lut[q[lo.r]], Lut[q[lo.g]], Lut[q[lo.b]]} * qa, cover += qa;
    }
    if (colour != nullptr) *colour = cover > 1e-5f ? c * (1.0f / cover) : V3{};
    return cover;
  };
  // The radial fit, as a table of how far the inner edge moves per direction.
  constexpr int kDirs = 180;
  std::array<float, kDirs> radD{}, radEdge{};
  const V2 radC{chinT.x, g.top + 0.55f * g.unit};
  const float radU = g.unit, radReach = 1.5f * g.unit;
  if (useLayer) {
    std::array<float, kDirs> rawD{};
    for (int i = 0; i < kDirs; ++i) {
      const float th = -3.14159265f + (static_cast<float>(i) + 0.5f) * (6.2831853f / kDirs);  // 0 = up
      const float dx = std::sin(th), dy = -std::cos(th);
      float rt = -1.0f, rd = -1.0f;
      for (float r = 0.0f; r < 1.6f * radU; r += 1.0f) {
        const auto px = static_cast<int32_t>(radC.x + dx * r), py = static_cast<int32_t>(radC.y + dy * r);
        if (px < 0 || py < 0 || px >= w || py >= h) break;
        if (faceKeep.at(px, py) > 0.5f) rt = r;
        if (rd < 0.0f) {
          const V2 q = toLayer(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);
          if (layerAt(q.x, q.y, nullptr) > 0.5f) rd = r;
        }
      }
      // Around the top and the sides of the face only (not below the chin).
      const float on = std::clamp((std::cos(th) + 0.35f) / 0.5f, 0.0f, 1.0f);
      rawD[static_cast<size_t>(i)] = (rt > 0.0f && rd > 0.0f) ? std::clamp(rd - rt, -0.04f * radU, 0.4f * radU) * on : 0.0f;
      radEdge[static_cast<size_t>(i)] = rd > 0.0f ? rd : (rt > 0.0f ? rt : 0.6f * radU);
    }
    for (int i = 0; i < kDirs; ++i) {  // smooth around the circle
      float sum = 0.0f, wsum = 0.0f;
      for (int k = -10; k <= 10; ++k) {
        const float wk = std::exp(-0.5f * static_cast<float>(k * k) / 12.0f);
        sum += wk * rawD[static_cast<size_t>((i + k + kDirs) % kDirs)], wsum += wk;
      }
      radD[static_cast<size_t>(i)] = sum / wsum;
    }
  }
  // Output grid point -> the grid point the similarity-placed layer is read at.
  auto unRadial = [&](float gx, float gy) {
    const float X = gx - radC.x, Y = gy - radC.y, R = std::sqrt(X * X + Y * Y);
    if (R < 1e-3f) return V2{gx, gy};
    const float th = std::atan2(X, -Y);
    const auto i = static_cast<size_t>(std::clamp(static_cast<int>((th + 3.14159265f) / 6.2831853f * kDirs), 0, kDirs - 1));
    float Rs = R;
    for (int it = 0; it < 4; ++it) Rs = R + radD[i] * std::clamp(1.0f - (Rs - radEdge[i]) / radReach, 0.0f, 1.0f);
    return V2{radC.x + X * Rs / R, radC.y + Y * Rs / R};
  };

  Layer L;
  {
    const float rs = std::min(1.0f, 560.0f / g.unit) * g.unit;  // pixels per face height on the layer
    float xl, xr2, yt, yb;
    if (useLayer) {
      // Wherever the placed photo reaches (and a margin for the radial fit).
      xl = 1e9f, xr2 = -1e9f, yt = 1e9f, yb = -1e9f;
      for (const auto& c : {V2{0, 0}, V2{static_cast<float>(lay->width()), 0}, V2{0, static_cast<float>(lay->height())}, V2{static_cast<float>(lay->width()), static_cast<float>(lay->height())}}) {
        const V2 p = toGrid(c.x, c.y);
        const float X = (p.x - g.cx) / g.unit, Y = (p.y - g.top) / g.unit;
        xl = std::min(xl, X), xr2 = std::max(xr2, X), yt = std::min(yt, Y), yb = std::max(yb, Y);
      }
      xl -= 0.1f, xr2 += 0.1f, yt -= 0.1f, yb += 0.1f;
    } else {
      const float xr = g.skullHalf + st->side + 0.5f;
      xl = -xr, xr2 = xr, yt = g.skullTop - st->top - 0.5f, yb = std::max(0.6f, 0.95f + st->len + 0.2f);
    }
    L.scale = rs, L.x0 = xl, L.y0 = yt;
    L.w = std::max(8, static_cast<int32_t>(std::ceil((xr2 - xl) * rs))), L.h = std::max(8, static_cast<int32_t>(std::ceil((yb - yt) * rs)));
    L.c.assign(static_cast<size_t>(L.w) * L.h, V3{});
    L.a.assign(static_cast<size_t>(L.w) * L.h, 0.0f);
  }
  // Where hair may be drawn: not over the face (below any fringe).
  Grid allow, sd;
  allow.init(L.w, L.h), sd.init(L.w, L.h);
  std::vector<uint8_t> inExtra(static_cast<size_t>(L.w) * L.h, 0);
  const float fringeLine = std::max(0.0f, st->fringe);
  for (int32_t y = 0; y < L.h; ++y)
    for (int32_t x = 0; x < L.w; ++x) {
      const float X = L.toX(static_cast<float>(x) + 0.5f), Y = L.toY(static_cast<float>(y) + 0.5f);
      const ShapeAt s = shapeAt(*st, g, X, Y);
      sd.at(x, y) = s.d;
      inExtra[static_cast<size_t>(y) * L.w + x] = s.extra ? 1 : 0;
      const float fx = g.cx + X * g.unit, fy = g.top + Y * g.unit;
      const float face = faceKeep.sample(fx, fy);
      const float fringeEdge = fringeLine + 0.02f * fbm(X * 30.0f, 0.0f, params.seed + 9);
      float ok = 1.0f;
      if (st->fringe <= 0.0f || Y > fringeEdge) ok = 1.0f - smooth(0.3f, 0.7f, face);
      allow.at(x, y) = ok;
    }
  // The surface's normal (for light): bulging from the outline inward.
  auto normalAt = [&](float px, float py) {
    const auto xi = std::clamp(static_cast<int32_t>(px), 1, L.w - 2), yi = std::clamp(static_cast<int32_t>(py), 1, L.h - 2);
    V2 gr{sd.at(xi + 1, yi) - sd.at(xi - 1, yi), sd.at(xi, yi + 1) - sd.at(xi, yi - 1)};
    gr = norm2(gr);
    const float depth = std::clamp(-sd.at(xi, yi) / 0.14f, 0.0f, 1.0f);
    const float k = 0.92f * (1.0f - depth);
    return norm3(V3{gr.x * k, gr.y * k, std::sqrt(std::max(0.05f, 1.0f - k * k))});
  };
  const V3 base = mix(ownHair, newColour, params.recolor ? newColourMix : 0.0f);
  std::mt19937 rng(params.seed * 7919u + 17u);
  std::uniform_real_distribution<float> U(0.0f, 1.0f);
  std::normal_distribution<float> N01(0.0f, 1.0f);
  const float px1 = 1.0f / L.scale;  // one layer pixel in face units
  // Interior: a dark under-layer, so strands never show the background.
  auto underlayer = [&](V3 colour, float inset) {
    for (int32_t y = 0; y < L.h; ++y)
      for (int32_t x = 0; x < L.w; ++x) {
        const float d = sd.at(x, y);
        const float k = smooth(-inset, -inset - 0.03f, d) * allow.at(x, y);
        if (k <= 0.0f) continue;
        const V3 nrm = normalAt(static_cast<float>(x), static_cast<float>(y));
        const size_t i = static_cast<size_t>(y) * L.w + x;
        const V3 c = colour * (0.55f + 0.45f * std::max(0.0f, dot3(nrm, kLight)));
        L.c[i] = c * k + L.c[i] * (1.0f - k), L.a[i] = k + L.a[i] * (1.0f - k);
      }
  };
  // Random points inside the shape (or its extras).
  // which: 0 the main shape, 1 the extras only, 2 both.
  auto inside = [&](int which, float margin) {
    for (int t = 0; t < 64; ++t) {
      const float x = U(rng) * static_cast<float>(L.w), y = U(rng) * static_cast<float>(L.h);
      const auto xi = static_cast<int32_t>(x), yi = static_cast<int32_t>(y);
      if (sd.at(xi, yi) > -margin) continue;
      const bool ex = inExtra[static_cast<size_t>(yi) * L.w + xi] != 0;
      if ((which == 0 && ex) || (which == 1 && !ex)) continue;
      return V2{x, y};
    }
    return V2{-1, -1};
  };
  auto areaOf = [&](int which) {
    size_t c = 0;
    for (size_t i = 0; i < sd.v.size(); ++i)
      if (sd.v[i] < 0.0f && (which == 2 || (inExtra[i] != 0) == (which == 1))) ++c;
    return static_cast<float>(c);
  };
  const float strandR = std::max(0.45f, 0.0011f * L.scale);  // a strand's radius on the layer

  // Strands (and the cap of every style but coils, ropes and the dome).
  auto drawStrands = [&](bool slickCap) {
    underlayer(base * 0.32f, 0.012f);
    const bool longHair = st->len > -0.5f;
    const float lenU = longHair ? 0.55f : 0.28f;
    // Buns and ponytails are strands too (puffs and curly buns are coils).
    const int which = (st->extra == Extra::TopKnot || st->extra == Extra::Ponytail) ? 2 : 0;
    const float area = areaOf(which);
    const auto count = static_cast<int32_t>(std::min(60000.0f, 7.0f * area / (lenU * L.scale * strandR * 2.0f)));
    struct S {
      V2 p;
      float depth, jit, phase;
    };
    std::vector<S> list;
    list.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
      const V2 p = inside(which, 0.0f);
      if (p.x < 0) continue;
      list.push_back({p, U(rng), std::exp(0.2f * N01(rng)), U(rng) * 6.2832f});
    }
    std::sort(list.begin(), list.end(), [](const S& x, const S& y) { return x.depth < y.depth; });
    const float step = std::max(0.5f, strandR);
    for (const S& s : list) {
      // Trace back to the strand's start, then draw forward.
      V2 p = s.p;
      const int steps = static_cast<int>(lenU * L.scale / step);
      for (int k = 0; k < steps / 2; ++k) {
        const V2 t = flowAt(*st, g, L.toX(p.x), L.toY(p.y));
        p = V2{p.x - t.x * step, p.y - t.y * step};
        if (p.x < 1 || p.y < 1 || p.x >= static_cast<float>(L.w - 1) || p.y >= static_cast<float>(L.h - 1)) break;
        if (sd.at(static_cast<int32_t>(p.x), static_cast<int32_t>(p.y)) > 0.0f) break;
      }
      const float shade = 0.55f + 0.45f * s.depth;
      for (int k = 0; k < steps; ++k) {
        if (p.x < 1 || p.y < 1 || p.x >= static_cast<float>(L.w - 1) || p.y >= static_cast<float>(L.h - 1)) break;
        const float d = sd.at(static_cast<int32_t>(p.x), static_cast<int32_t>(p.y));
        if (d > 0.03f) break;
        const float X = L.toX(p.x), Y = L.toY(p.y);
        V2 t = flowAt(*st, g, X, Y);
        const V2 perp{-t.y, t.x};
        const float sAlong = static_cast<float>(k) * step * px1;
        float off = st->wave * std::sin(6.2832f * Y / std::max(st->waveLen, 0.05f) + 0.6f * vnoise(X * 4.0f, 0.0f, params.seed + 3));
        off += st->curl * std::sin(6.2832f * sAlong / 0.07f + s.phase);
        const float slope = st->wave * 6.2832f / std::max(st->waveLen, 0.05f) * std::cos(6.2832f * Y / std::max(st->waveLen, 0.05f)) +
                            st->curl * 6.2832f / 0.07f * std::cos(6.2832f * sAlong / 0.07f + s.phase);
        t = norm2(V2{t.x + perp.x * slope * 0.5f, t.y + perp.y * slope * 0.5f});
        const float qx = p.x + perp.x * off * L.scale, qy = p.y + perp.y * off * L.scale;
        const float taper = smooth(0.0f, 0.12f, static_cast<float>(k) / static_cast<float>(steps)) *
                            smooth(0.0f, 0.2f, 1.0f - static_cast<float>(k) / static_cast<float>(steps));
        const float edge = smooth(0.03f, 0.0f, d);
        const V3 nrm = normalAt(qx, qy);
        const V3 c = litFibre(base * s.jit, t, nrm, st->gloss * (slickCap ? 1.0f : 1.0f), shade);
        L.dot(qx, qy, strandR, c, 0.55f * taper * edge, &allow);
        const V2 tf = flowAt(*st, g, X, Y);
        p = V2{p.x + tf.x * step, p.y + tf.y * step};
      }
    }
  };

  // Coils: tight curls, everywhere (afros) or in the extras (puffs, buns).
  auto drawCoils = [&](bool extraOnly, float loose) {
    if (!extraOnly) underlayer(base * 0.3f, 0.02f);
    else
      for (int32_t y = 0; y < L.h; ++y)
        for (int32_t x = 0; x < L.w; ++x) {
          const size_t i = static_cast<size_t>(y) * L.w + x;
          if (!inExtra[i] || sd.v[i] > -0.02f) continue;
          const V3 nrm = normalAt(static_cast<float>(x), static_cast<float>(y));
          L.c[i] = base * (0.3f * (0.55f + 0.45f * std::max(0.0f, dot3(nrm, kLight)))), L.a[i] = 1.0f;
        }
    const float area = areaOf(extraOnly ? 1 : 0);
    const float rcU = 0.016f * loose;
    const float coilLen = 6.2832f * rcU * 2.2f;
    const auto count = static_cast<int32_t>(std::min(90000.0f, 9.0f * area / (coilLen * L.scale * strandR * 2.0f)));
    struct C {
      V2 p;
      float depth;
    };
    std::vector<C> list;
    for (int32_t i = 0; i < count; ++i) {
      const V2 p = inside(extraOnly ? 1 : 0, -0.03f);
      if (p.x < 0) continue;
      const float d = sd.at(static_cast<int32_t>(p.x), static_cast<int32_t>(p.y));
      list.push_back({p, std::clamp(0.6f * U(rng) + 0.4f * smooth(-0.25f, 0.0f, d), 0.0f, 1.0f)});
    }
    std::sort(list.begin(), list.end(), [](const C& x, const C& y) { return x.depth < y.depth; });
    for (const C& cl : list) {
      const float r = rcU * (0.7f + 0.6f * U(rng)) * L.scale;
      const float turns = 1.6f + 1.2f * U(rng);
      const float ang0 = U(rng) * 6.2832f;
      const V2 drift = norm2(V2{N01(rng), N01(rng)});
      // Coils grow in clumps: neighbours share their light.
      const float cX = L.toX(cl.p.x), cY = L.toY(cl.p.y);
      const float clump = 0.75f + 0.35f * fbm(cX * 11.0f, cY * 11.0f, params.seed + 50);
      const float jit = std::exp(0.18f * N01(rng)) * clump;
      const float shade = 0.45f + 0.55f * cl.depth;
      const float total = turns * 6.2832f;
      const float dth = std::max(0.08f, std::max(0.5f, strandR) / std::max(r, 1.0f));
      for (float th = 0.0f; th < total; th += dth) {
        const float f01 = th / total;
        const float px = cl.p.x + r * std::cos(ang0 + th) + drift.x * r * 1.6f * f01;
        const float py = cl.p.y + r * 0.8f * std::sin(ang0 + th) + drift.y * r * 1.6f * f01;
        if (px < 1 || py < 1 || px >= static_cast<float>(L.w - 1) || py >= static_cast<float>(L.h - 1)) continue;
        const float d = sd.at(static_cast<int32_t>(px), static_cast<int32_t>(py));
        if (d > 0.045f) continue;
        const V2 t = norm2(V2{-std::sin(ang0 + th) + drift.x * 0.25f, 0.8f * std::cos(ang0 + th) + drift.y * 0.25f});
        const V3 nrm = normalAt(px, py);
        const V3 c = litFibre(base * jit, t, nrm, st->gloss, shade);
        const float taper = smooth(0.0f, 0.1f, f01) * smooth(0.0f, 0.1f, 1.0f - f01);
        L.dot(px, py, strandR * 1.2f, c, 0.6f * taper * smooth(0.045f, 0.0f, d), &allow);
      }
    }
  };

  // A rope (a loc, a braid, a twist) along a path, with its surface.
  auto drawRope = [&](const std::vector<V2>& path, float widthU, RopeKind kind, uint32_t id, float shade) {
    if (path.size() < 2) return;
    float sAcc = 0.0f;
    for (size_t k = 0; k + 1 < path.size(); ++k) {
      const V2 p = path[k], q = path[k + 1];
      const float seg = std::hypot(q.x - p.x, q.y - p.y);
      if (seg <= 1e-4f) continue;
      const V2 t{(q.x - p.x) / seg, (q.y - p.y) / seg};
      const V2 perp{-t.y, t.x};
      const float sU = sAcc * px1;
      float rU = 0.5f * widthU;
      if (st->freeform) rU *= 1.0f + 0.28f * fbm(sU * 9.0f, static_cast<float>(id) * 1.7f, params.seed + 21);
      else if (kind == RopeKind::Locs) rU *= 1.0f + 0.1f * fbm(sU * 12.0f, static_cast<float>(id) * 1.3f, params.seed + 21);
      // Ends taper.
      const float total = static_cast<float>(path.size()) * px1 * 0.5f;
      rU *= 0.55f + 0.45f * smooth(0.0f, 0.05f, total - sU);
      const float r = rU * L.scale;
      const int32_t xa = static_cast<int32_t>(std::floor(p.x - r - 1)), xb = static_cast<int32_t>(std::ceil(p.x + r + 1));
      const int32_t ya = static_cast<int32_t>(std::floor(p.y - r - 1)), yb = static_cast<int32_t>(std::ceil(p.y + r + 1));
      for (int32_t y = std::max(0, ya); y <= std::min(L.h - 1, yb); ++y)
        for (int32_t x = std::max(0, xa); x <= std::min(L.w - 1, xb); ++x) {
          const float dx = static_cast<float>(x) + 0.5f - p.x, dy = static_cast<float>(y) + 0.5f - p.y;
          const float dist = std::sqrt(dx * dx + dy * dy);
          float cov = std::clamp(r + 0.5f - dist, 0.0f, 1.0f) * allow.at(x, y);
          if (cov <= 0.0f) continue;
          const float ov = std::clamp((dx * perp.x + dy * perp.y) / std::max(r, 0.5f), -1.0f, 1.0f);
          const float along = sU + (dx * t.x + dy * t.y) * px1;
          const V3 ng = normalAt(static_cast<float>(x), static_cast<float>(y));
          const V3 nr = norm3(V3{perp.x * ov, perp.y * ov, 0.0f} * 1.0f + ng * std::sqrt(std::max(0.0f, 1.0f - ov * ov)));
          float tex;
          switch (kind) {
            case RopeKind::Braids:
            case RopeKind::Cornrows: {
              const float lambda = widthU * 0.9f;
              const float g2 = fract(along / lambda + std::fabs(ov) * 0.55f + (ov < 0.0f ? 0.5f : 0.0f));
              tex = 0.55f + 0.45f * std::sin(3.1416f * g2);
              tex *= 0.9f + 0.1f * vnoise(along * 400.0f, ov * 8.0f, params.seed + id);
              break;
            }
            case RopeKind::Twists:
            case RopeKind::Knots: {
              const float lambda = widthU * 1.3f;
              const float g2 = fract(along / lambda + ov * 0.5f);
              tex = 0.55f + 0.45f * std::sin(3.1416f * g2);
              tex *= 0.9f + 0.1f * vnoise(along * 400.0f, ov * 8.0f, params.seed + id);
              break;
            }
            default:  // locs: matted, bumpy fibres
              tex = 0.78f + 0.22f * fbm(along * 60.0f, ov * 3.0f + static_cast<float>(id), params.seed + 5) +
                    0.12f * vnoise(along * 500.0f, ov * 14.0f, params.seed + 6);
              break;
          }
          const float diff = 0.25f + 0.75f * std::max(0.0f, dot3(nr, kLight));
          const float spec = std::pow(std::max(0.0f, dot3(nr, kHalf)), 16.0f) * st->gloss;
          const V3 c = base * (tex * diff * shade) + (base * 1.6f + grey(0.02f)) * (0.35f * spec);
          const size_t i = static_cast<size_t>(y) * L.w + x;
          L.c[i] = c * cov + L.c[i] * (1.0f - cov), L.a[i] = cov + L.a[i] * (1.0f - cov);
        }
      sAcc += seg;
    }
    // Stray fibres on locs.
    if (kind == RopeKind::Locs) {
      for (size_t k = 0; k + 1 < path.size(); k += std::max<size_t>(3, static_cast<size_t>(0.02f * L.scale))) {
        const V2 p = path[k], q = path[k + 1];
        const V2 t = norm2(V2{q.x - p.x, q.y - p.y});
        const float side = U(rng) < 0.5f ? -1.0f : 1.0f;
        const float ang = side * (0.4f + 0.8f * U(rng));
        const V2 dir{t.x * std::cos(ang) - t.y * std::sin(ang), t.x * std::sin(ang) + t.y * std::cos(ang)};
        const float start = 0.45f * widthU * L.scale;
        V2 fp{p.x - t.y * side * start, p.y + t.x * side * start};
        const float lenPx = (0.008f + 0.015f * U(rng)) * L.scale;
        for (float s = 0.0f; s < lenPx; s += 0.6f) {
          const V3 nrm = normalAt(fp.x, fp.y);
          L.dot(fp.x, fp.y, std::max(0.35f, strandR * 0.7f), litFibre(base * 1.15f, dir, nrm, 0.3f, shade), 0.45f * (1.0f - s / lenPx), &allow);
          fp = V2{fp.x + dir.x * 0.6f, fp.y + dir.y * 0.6f};
        }
      }
    }
  };
  // A rope's path from a point, along the flow (or toward the crown), on the layer.
  auto ropePath = [&](V2 start, bool towardCrown, float maxLenU) {
    std::vector<V2> path;
    V2 p = start;
    const float step = 0.7f;
    const auto steps = static_cast<int>(maxLenU * L.scale / step);
    for (int k = 0; k < steps; ++k) {
      if (p.x < 1 || p.y < 1 || p.x >= static_cast<float>(L.w - 1) || p.y >= static_cast<float>(L.h - 1)) break;
      if (sd.at(static_cast<int32_t>(p.x), static_cast<int32_t>(p.y)) > 0.01f) break;
      path.push_back(p);
      V2 t = flowAt(*st, g, L.toX(p.x), L.toY(p.y));
      if (towardCrown) t = V2{-t.x, -t.y};
      if (st->freeform) {
        const float wob = 0.35f * vnoise(static_cast<float>(k) * 0.02f, start.x * 0.1f, params.seed + 30);
        t = norm2(V2{t.x + t.y * wob, t.y - t.x * wob});
      }
      p = V2{p.x + t.x * step, p.y + t.y * step};
    }
    return path;
  };

  // A head without hair (bald, or the faded sides of a high top): the
  // person's own skin carried up from the forehead and temples (a smooth
  // fill, so it matches where it meets them), rounded by the light relative
  // to the forehead, with a soft sheen and skin grain. `fade`: how much
  // close-cut hair shows, most at the top, none toward the ears.
  auto drawScalp = [&](float fade) {
    // A shaved head: the person's own skin carried up from the forehead and
    // temples (a smooth fill, so it matches where it meets them), rounded by
    // the light relative to the forehead, with a soft sheen and skin grain.
    Grid faceWide = faceKeep;
    blurGrid(faceWide, std::max(1.0f, 0.03f * g.unit));
    std::vector<V3> skinFill = col;
    {
      std::vector<float> known(n);
      for (size_t i = 0; i < n; ++i) known[i] = (faceKeep.v[i] > 0.9f && alpha[i] > 0.9f && skinLike(col[i]) > 0.5f) ? 1.0f : 0.0f;
      pushPull(skinFill, known, w, h);
    }
    auto fillAt = [&](float X, float Y) {
      const auto x = std::clamp(static_cast<int32_t>(g.cx + X * g.unit), 0, w - 1), y = std::clamp(static_cast<int32_t>(g.top + Y * g.unit), 0, h - 1);
      return skinFill[static_cast<size_t>(y) * w + x];
    };
    const V3 nHair = norm3(V3{0.0f, 0.25f, 1.0f});  // the forehead's normal, near the hairline
    const float foreLight = 0.55f + 0.45f * std::max(0.0f, dot3(nHair, kLight));
    for (int32_t y = 0; y < L.h; ++y)
      for (int32_t x = 0; x < L.w; ++x) {
        const float X = L.toX(static_cast<float>(x) + 0.5f), Y = L.toY(static_cast<float>(y) + 0.5f);
        float cov = smooth(0.006f, -0.006f, sd.at(x, y));
        // Over the face, fading into it across a band (no seam).
        const float fk = faceWide.sample(g.cx + X * g.unit, g.top + Y * g.unit);
        cov *= 1.0f - smooth(0.45f, 0.97f, fk);
        if (cov <= 0.0f) continue;
        const float ex = X / (g.skullHalf + 0.005f), ey = Y < 0.2f ? (Y - 0.2f) / (0.2f - g.skullTop + 0.005f) : 0.0f;
        const float rr = std::min(1.0f, std::sqrt(ex * ex + ey * ey));
        const V3 nrm = norm3(V3{ex, ey, std::sqrt(std::max(0.02f, 1.0f - rr * rr))});
        const float light = std::clamp((0.55f + 0.45f * std::max(0.0f, dot3(nrm, kLight))) / foreLight, 0.7f, 1.1f) * (1.0f - 0.18f * smooth(0.75f, 1.0f, rr));
        const float spec = std::pow(std::max(0.0f, dot3(nrm, kHalf)), 24.0f);
        const float tex = 1.0f + 0.03f * fbm(X * 160.0f, Y * 160.0f, params.seed + 41) + 0.018f * vnoise(X * 520.0f, Y * 520.0f, params.seed + 42);
        const float stub = 1.0f - 0.05f * smooth(0.6f, 1.0f, vnoise(X * 700.0f, Y * 700.0f, params.seed + 40)) * smooth(0.3f, 0.9f, rr);
        const V3 sk = fillAt(X, Y);
        V3 c = sk * (light * tex * stub) + sk * (0.3f * spec);
        if (fade > 0.0f) {
          const float dens = fade * (1.0f - smooth(-0.02f, 0.34f, Y));
          const float dots = smooth(0.2f, 0.9f, vnoise(X * 650.0f, Y * 650.0f, params.seed + 43) * 0.5f + 0.5f);
          c = mix(c, base * (0.9f * light), std::clamp(dens * (0.4f + 0.6f * dots), 0.0f, 1.0f));
        }
        const size_t i = static_cast<size_t>(y) * L.w + x;
        L.c[i] = c * cov + L.c[i] * (1.0f - cov), L.a[i] = cov + L.a[i] * (1.0f - cov);
      }
  };

  if (useLayer) {
    // The photographed hair, read through the fit. Its own colour stays
    // unless a colour is chosen (then each strand keeps its light).
    double ll = 0, ln = 0;
    for (int32_t y = 0; y < L.h; ++y)
      for (int32_t x = 0; x < L.w; ++x) {
        const float X = L.toX(static_cast<float>(x) + 0.5f), Y = L.toY(static_cast<float>(y) + 0.5f);
        const V2 p = unRadial(g.cx + X * g.unit, g.top + Y * g.unit);
        const V2 q = toLayer(p.x, p.y);
        V3 c;
        const float a2 = layerAt(q.x, q.y, &c);
        const size_t i = static_cast<size_t>(y) * L.w + x;
        L.c[i] = c, L.a[i] = a2;
        if (a2 > 0.8f) ll += std::log(std::max(lum(c), 1e-4f)), ln += 1;
      }
    if (params.recolor && ln > 30) {
      const float meanLog = static_cast<float>(ll / ln);
      for (size_t i = 0; i < L.a.size(); ++i)
        if (L.a[i] > 0.0f) L.c[i] = recolourHair(L.c[i], meanLog, newColour, newColourMix);
    }
  } else if (st->kind == Kind::Strands) {
    drawStrands(st->slick);
    if (st->extra == Extra::Puff || st->extra == Extra::CoilBun) drawCoils(true, 1.0f);
  } else if (st->kind == Kind::Coils) {
    drawCoils(false, st->afro ? 1.0f : 1.7f);
  } else if (st->kind == Kind::Ropes) {
    const float wU = st->ropeW;
    if (st->rope == RopeKind::Cornrows) {
      // Rows from the hairline back over the head, scalp between them;
      // braids falling from the back.
      underlayer(skin * 0.5f, -0.03f);
      // The braids below the cap fall over a dark base, not scalp.
      for (int32_t y = 0; y < L.h; ++y)
        for (int32_t x = 0; x < L.w; ++x)
          if (L.toY(static_cast<float>(y) + 0.5f) > 0.36f) L.c[static_cast<size_t>(y) * L.w + x] = base * 0.22f * L.a[static_cast<size_t>(y) * L.w + x];
      const int rows = std::max(6, static_cast<int>(2.0f * g.skullHalf / (wU * 1.25f)));
      for (int i = 0; i < rows; ++i) {
        const float X = -g.skullHalf * 0.97f + (static_cast<float>(i) + 0.5f) * (1.94f * g.skullHalf / static_cast<float>(rows));
        const float Y = hairline(X) + 0.01f;
        // Straight back over the head, converging a little toward the crown.
        std::vector<V2> path;
        V2 p{(X - L.x0) * L.scale, (Y - L.y0) * L.scale};
        const V2 dir = norm2(V2{-X * 0.35f, -1.0f});
        for (int k = 0; k < static_cast<int>(1.2f * L.scale / 0.7f); ++k) {
          if (p.x < 1 || p.y < 1 || p.x >= static_cast<float>(L.w - 1) || p.y >= static_cast<float>(L.h - 1)) break;
          if (sd.at(static_cast<int32_t>(p.x), static_cast<int32_t>(p.y)) > 0.004f) break;
          path.push_back(p);
          p = V2{p.x + dir.x * 0.7f, p.y + dir.y * 0.7f};
        }
        drawRope(path, wU, RopeKind::Cornrows, static_cast<uint32_t>(i), 0.95f);
      }
      if (st->len > -0.5f) {
        const int hang = std::max(8, static_cast<int>(3.2f * (g.skullHalf + 0.1f) / wU));
        for (int i = 0; i < hang; ++i) {
          const float X = -(g.skullHalf + 0.08f) + (static_cast<float>(i) + 0.5f) * (2.0f * (g.skullHalf + 0.08f) / static_cast<float>(hang));
          if (std::fabs(X) < a * 0.85f) continue;  // the face is in front of them
          const V2 s{(X - L.x0) * L.scale, (0.25f - L.y0) * L.scale};
          drawRope(ropePath(s, false, 1.8f), wU, RopeKind::Braids, static_cast<uint32_t>(100 + i), 0.85f);
        }
      }
    } else if (st->rope == RopeKind::Knots) {
      // A sleek parted base, then the knots: coiled ropes.
      underlayer(base * 0.32f, 0.0f);
      const float kx[5] = {-0.3f, 0.0f, 0.3f, -0.42f, 0.42f}, ky[5] = {0.12f, 0.0f, 0.12f, 0.4f, 0.4f};
      for (int k = 0; k < 5; ++k) {
        const V2 c{(kx[k] * g.skullHalf / 0.46f - L.x0) * L.scale, (g.skullTop + ky[k] * 0.6f - L.y0) * L.scale};
        std::vector<V2> path;
        const float R0 = 0.085f * L.scale;
        for (float th = 0.0f; th < 3.0f * 6.2832f; th += 0.03f) {
          const float r = R0 * (1.0f - th / (3.2f * 6.2832f));
          path.push_back(V2{c.x + r * std::cos(th), c.y + r * std::sin(th)});
        }
        drawRope(path, wU, RopeKind::Knots, static_cast<uint32_t>(k), 1.0f);
      }
    } else if (st->highTop) {
      // High-top locs: the sides faded close to the skin; short, even locs
      // rising from the top of the head and falling forward and out.
      drawScalp(0.75f);
      for (int32_t y = 0; y < L.h; ++y)
        for (int32_t x = 0; x < L.w; ++x) {
          const float X = L.toX(static_cast<float>(x) + 0.5f), Y = L.toY(static_cast<float>(y) + 0.5f);
          const float k = smooth(-0.01f, -0.04f, scalpSd(g, X, Y)) * smooth(0.06f, -0.02f, Y) * (1.0f - smooth(g.skullHalf * 0.6f, g.skullHalf * 0.85f, std::fabs(X))) * allow.at(x, y);
          if (k <= 0.0f) continue;
          const size_t i = static_cast<size_t>(y) * L.w + x;
          L.c[i] = base * 0.22f * k + L.c[i] * (1.0f - k), L.a[i] = k + L.a[i] * (1.0f - k);
        }
      struct R {
        V2 p;
        float x, y;
      };
      std::vector<R> roots;
      const float spacing = wU * 0.95f;
      for (float Y = g.skullTop + 0.03f; Y < 0.03f; Y += spacing)
        for (float X = -g.skullHalf * 0.72f; X <= g.skullHalf * 0.72f; X += spacing) {
          const float jx = X + (U(rng) - 0.5f) * spacing * 0.4f, jy = Y + (U(rng) - 0.5f) * spacing * 0.4f;
          if (scalpSd(g, jx, jy) > -0.025f) continue;
          roots.push_back({V2{(jx - L.x0) * L.scale, (jy - L.y0) * L.scale}, jx, jy});
        }
      // From the back of the top to the front: the front ones lie over.
      std::sort(roots.begin(), roots.end(), [](const R& r1, const R& r2) { return r1.y < r2.y; });
      uint32_t id = 0;
      for (const R& r : roots) {
        std::vector<V2> path;
        V2 p = r.p;
        // The middle ones fall to one side or the other too.
        const float lean = std::fabs(r.x) < 0.08f ? (U(rng) < 0.5f ? -0.35f : 0.35f) : 0.0f;
        V2 dir = norm2(V2{r.x * 2.2f + lean + 0.2f * (U(rng) - 0.5f), -0.7f});
        const float lenU = 0.28f + 0.1f * U(rng);
        const float stepPx = 0.7f;
        const auto steps = static_cast<int>(lenU * L.scale / stepPx);
        for (int k = 0; k < steps; ++k) {
          if (p.x < 1 || p.y < 1 || p.x >= static_cast<float>(L.w - 1) || p.y >= static_cast<float>(L.h - 1)) break;
          path.push_back(p);
          // Gravity bends them over.
          dir = norm2(V2{dir.x * (1.0f + 0.6f * stepPx / L.scale), dir.y + 9.0f * stepPx / L.scale});
          p = V2{p.x + dir.x * stepPx, p.y + dir.y * stepPx};
        }
        drawRope(path, wU * (0.92f + 0.16f * U(rng)), RopeKind::Locs, id++, 0.65f + 0.35f * smooth(g.skullTop, 0.03f, r.y));
      }
    } else {
      // Locs, braids, twists: roots over the scalp; those nearer the face on top.
      underlayer(base * 0.25f, 0.01f);
      struct R {
        V2 p;
        float depth;
      };
      std::vector<R> roots;
      const float spacing = wU * 0.85f;
      for (float Y = g.skullTop + 0.02f; Y < 0.3f; Y += spacing)
        for (float X = -g.skullHalf - st->side; X <= g.skullHalf + st->side; X += spacing) {
          const float jx = X + (U(rng) - 0.5f) * spacing * 0.6f, jy = Y + (U(rng) - 0.5f) * spacing * 0.6f;
          const V2 p{(jx - L.x0) * L.scale, (jy - L.y0) * L.scale};
          if (p.x < 1 || p.y < 1 || p.x >= static_cast<float>(L.w - 1) || p.y >= static_cast<float>(L.h - 1)) continue;
          if (sd.at(static_cast<int32_t>(p.x), static_cast<int32_t>(p.y)) > -0.01f) continue;
          roots.push_back({p, std::clamp((jy - g.skullTop) / 0.6f + 0.25f * (U(rng) - 0.5f), 0.0f, 1.0f)});
        }
      std::sort(roots.begin(), roots.end(), [](const R& x, const R& y) { return x.depth < y.depth; });
      uint32_t id = 0;
      for (const R& r : roots) {
        drawRope(ropePath(r.p, false, 2.2f), wU * (0.85f + 0.3f * U(rng)), st->rope, id++, 0.6f + 0.4f * r.depth);
      }
    }
  } else {
    drawScalp(0.12f);
  }
  // Like the photo: a touch soft, a touch of grain (a photographed style
  // already is).
  if (!useLayer) {
    Grid ch[4];
    for (auto& gch : ch) gch.init(L.w, L.h);
    for (size_t i = 0; i < L.a.size(); ++i) ch[0].v[i] = L.c[i].r, ch[1].v[i] = L.c[i].g, ch[2].v[i] = L.c[i].b, ch[3].v[i] = L.a[i];
    for (auto& gch : ch) blurGrid(gch, 0.45f);
    for (size_t i = 0; i < L.a.size(); ++i) {
      const float grain = 1.0f + 0.03f * N01(rng);
      L.c[i] = V3{ch[0].v[i], ch[1].v[i], ch[2].v[i]} * grain, L.a[i] = ch[3].v[i];
    }
  }
  // The hair's soft shadow on the face and neck.
  Grid shadow;
  shadow.init(L.w, L.h);
  {
    const auto off = static_cast<int32_t>(0.025f * L.scale);
    for (int32_t y = 0; y < L.h; ++y)
      for (int32_t x = 0; x < L.w; ++x) shadow.at(x, y) = y - off >= 0 ? L.a[static_cast<size_t>(y - off) * L.w + x] : 0.0f;
    blurGrid(shadow, 0.025f * L.scale);
  }

  // ---- A photographed style's hairline need not sit where the old one did:
  // the forehead and temples left between the face and the new hair are the
  // person's skin, carried up from the face.
  if (useLayer) {
    std::vector<float> la(n, 0.0f);
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const float X = (static_cast<float>(x) + 0.5f - g.cx) / g.unit, Y = (static_cast<float>(y) + 0.5f - g.top) / g.unit;
        const float lx = (X - L.x0) * L.scale, ly = (Y - L.y0) * L.scale;
        if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(L.w) || ly >= static_cast<float>(L.h)) continue;
        la[static_cast<size_t>(y) * w + x] = L.a[static_cast<size_t>(ly) * L.w + static_cast<size_t>(lx)];
      }
    // Inside the ring the new hair and the face make: between them along the
    // row and along the column, above the chin.
    std::vector<uint8_t> rowIn(n, 0), colIn(n, 0);
    auto solidAt = [&](size_t i) { return la[i] > 0.5f || faceKeep.v[i] > 0.5f; };
    const auto chinRow = static_cast<int32_t>(std::clamp(chinY, 0.0f, static_cast<float>(h)));
    for (int32_t y = 0; y < chinRow; ++y) {
      int32_t l = -1, r = -1;
      for (int32_t x = 0; x < w; ++x)
        if (solidAt(static_cast<size_t>(y) * w + x)) (l < 0 ? l : r) = x, r = x;
      for (int32_t x = std::max(l, 0); l >= 0 && x <= r; ++x) rowIn[static_cast<size_t>(y) * w + x] = 1;
    }
    for (int32_t x = 0; x < w; ++x) {
      int32_t t = -1, b = -1;
      for (int32_t y = 0; y < chinRow; ++y)
        if (solidAt(static_cast<size_t>(y) * w + x)) (t < 0 ? t : b) = y, b = y;
      for (int32_t y = std::max(t, 0); t >= 0 && y <= b; ++y) colIn[static_cast<size_t>(y) * w + x] = 1;
    }
    Grid gap;
    gap.init(w, h);
    for (size_t i = 0; i < n; ++i) gap.v[i] = (rowIn[i] && colIn[i] && la[i] < 0.5f && faceKeep.v[i] < 0.5f) ? 1.0f : 0.0f;
    blurGrid(gap, std::max(0.7f, 0.004f * g.unit));
    for (size_t i = 0; i < n; ++i) {
      if (gap.v[i] <= 0.02f || skinOut.empty()) continue;
      const float k = std::min(1.0f, gap.v[i] * 1.6f);
      if (k <= fillMix.v[i] && fillFace[i]) continue;
      fill[i] = mix(fill[i], skinOut[i], fillMix.v[i] > 0.0f ? k : 1.0f);
      fillMix.v[i] = std::max(fillMix.v[i], k), keepA.v[i] = std::max(keepA.v[i], k);
      fillFace[i] = 1, hasOff[i] = 0;
    }
  }

  // ---- 3. Composite at the cut-out's own size.
  const float invF = 1.0f / static_cast<float>(f);
  // The texture borrowed under the hair: the fine detail of the source spot
  // (its colour less its own smooth shading), at full size, laid on the
  // filled shading. Source rows are copied first (the composite writes the
  // cut-out in place).
  Grid lowR, lowG, lowB, fillR, fillG, fillB;
  lowR.init(w, h), lowG.init(w, h), lowB.init(w, h), fillR.init(w, h), fillG.init(w, h), fillB.init(w, h);
  for (size_t i = 0; i < n; ++i) lowR.v[i] = col[i].r, lowG.v[i] = col[i].g, lowB.v[i] = col[i].b, fillR.v[i] = fill[i].r, fillG.v[i] = fill[i].g, fillB.v[i] = fill[i].b;
  blurGrid(lowR, 1.2f), blurGrid(lowG, 1.2f), blurGrid(lowB, 1.2f);
  int32_t srcY0 = H, srcY1 = -1;
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      if (!hasOff[i]) continue;
      srcY0 = std::min(srcY0, (y + offY[i] - 1) * f), srcY1 = std::max(srcY1, (y + offY[i] + 2) * f);
    }
  srcY0 = std::clamp(srcY0, 0, H - 1), srcY1 = std::clamp(srcY1, -1, H - 1);
  std::vector<uint8_t> srcRows;
  if (srcY1 >= srcY0) {
    srcRows.resize(static_cast<size_t>(srcY1 - srcY0 + 1) * W * 4);
    for (int32_t y = srcY0; y <= srcY1; ++y) std::memcpy(&srcRows[static_cast<size_t>(y - srcY0) * W * 4], cutout.row(y), static_cast<size_t>(W) * 4);
  }
  auto texture = [&](int32_t x, int32_t y, size_t gi) {
    const int32_t sx = x + offX[gi] * f, sy = y + offY[gi] * f;
    const V3 none{1.0f, 1.0f, 1.0f};
    if (sx < 0 || sx >= W || sy < srcY0 || sy > srcY1) return none;
    const uint8_t* q = &srcRows[(static_cast<size_t>(sy - srcY0) * W + sx) * 4];
    if (q[3] < 230) return none;
    const float gx = (static_cast<float>(sx) + 0.5f) / static_cast<float>(f), gy = (static_cast<float>(sy) + 0.5f) / static_cast<float>(f);
    // The detail as a ratio to its own smooth shading (so it sits on any shading).
    auto ratio = [](float v, float smoothV) { return std::clamp(v / std::max(smoothV, 1e-3f), 0.3f, 2.5f); };
    return V3{ratio(Lut[q[o.r]], lowR.sample(gx, gy)), ratio(Lut[q[o.g]], lowG.sample(gx, gy)), ratio(Lut[q[o.b]], lowB.sample(gx, gy))};
  };
  const bool done = parallelFor(H, token, [&](int32_t b0, int32_t b1) {
    for (int32_t y = b0; y < b1; ++y) {
      uint8_t* p = cutout.row(y);
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* q = p + x * 4;
        const float gx = (static_cast<float>(x) + 0.5f) * invF, gy = (static_cast<float>(y) + 0.5f) * invF;
        V3 c{Lut[q[o.r]], Lut[q[o.g]], Lut[q[o.b]]};
        float aP = static_cast<float>(q[3]) / 255.0f;
        // Own hair out; body filled in where hair lay over it.
        const float km = keepA.sample(gx, gy);
        const float fillK = fillMix.sample(gx, gy);
        if (fillK > 0.0f) {
          const auto gi = static_cast<size_t>(std::clamp(static_cast<int32_t>(gy), 0, h - 1)) * w + std::clamp(static_cast<int32_t>(gx), 0, w - 1);
          V3 under{fillR.sample(gx, gy), fillG.sample(gx, gy), fillB.sample(gx, gy)};
          if (hasOff[gi]) {
            const V3 t = texture(x, y, gi);
            const float k = fillFace[gi] ? 0.6f : 1.0f;  // a cheek: gentler (skin is fine-grained)
            under = V3{under.r * (1.0f + k * (t.r - 1.0f)), under.g * (1.0f + k * (t.g - 1.0f)), under.b * (1.0f + k * (t.b - 1.0f))};
          }
          c = mix(c, under, fillK);
          aP = std::max(aP, fillK);
        }
        aP = std::clamp(aP * km, 0.0f, 1.0f);
        // The hair layer here.
        const float X = (gx - g.cx) / g.unit, Y = (gy - g.top) / g.unit;
        const float lx = (X - L.x0) * L.scale, ly = (Y - L.y0) * L.scale;
        V3 hc{};
        float ha = 0.0f, sh = 0.0f;
        if (lx >= 0.0f && ly >= 0.0f && lx < static_cast<float>(L.w) && ly < static_cast<float>(L.h)) {
          const float fx = std::clamp(lx - 0.5f, 0.0f, static_cast<float>(L.w - 1)), fy = std::clamp(ly - 0.5f, 0.0f, static_cast<float>(L.h - 1));
          const auto x0 = static_cast<int32_t>(fx), y0 = static_cast<int32_t>(fy);
          const int32_t x1 = std::min(L.w - 1, x0 + 1), y1 = std::min(L.h - 1, y0 + 1);
          const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
          const float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
          auto I = [&](int32_t xx, int32_t yy) { return static_cast<size_t>(yy) * L.w + xx; };
          hc = L.c[I(x0, y0)] * w00 + L.c[I(x1, y0)] * w10 + L.c[I(x0, y1)] * w01 + L.c[I(x1, y1)] * w11;
          ha = L.a[I(x0, y0)] * w00 + L.a[I(x1, y0)] * w10 + L.a[I(x0, y1)] * w01 + L.a[I(x1, y1)] * w11;
          sh = shadow.sample(lx, ly);
        }
        // Behind the neck and chest (softly: no corner where it changes).
        // (A photographed style already shows only the hair in front.)
        const float behind = useLayer ? 0.0f : smooth(0.86f, 0.98f, Y) * (1.0f - smooth(a * 0.8f, a * 1.05f, std::fabs(X)));
        // Shadow on the person where the hair doesn't cover them.
        if (sh > 0.0f) c = c * (1.0f - 0.35f * sh * (1.0f - ha) * (1.0f - behind));
        V3 outc;
        float outa;
        if (ha <= 0.0f) {
          outc = c, outa = aP;
        } else {
          const float aF = ha + aP * (1.0f - ha);
          const V3 cF = aF > 0.0f ? (hc + c * (aP * (1.0f - ha))) * (1.0f / aF) : c;
          const float aB = aP + ha * (1.0f - aP);
          const V3 cB = aB > 0.0f ? (c * aP + hc * (1.0f - aP)) * (1.0f / aB) : c;
          outa = aF + (aB - aF) * behind;
          outc = mix(cF, cB, behind);
        }
        q[o.r] = toByte(toGamma(std::max(outc.r, 0.0f))), q[o.g] = toByte(toGamma(std::max(outc.g, 0.0f))),
        q[o.b] = toByte(toGamma(std::max(outc.b, 0.0f))), q[3] = toByte(outa);
      }
    }
  });
  return done ? ok() : cancelledError();
}

}  // namespace photoplus::studio
