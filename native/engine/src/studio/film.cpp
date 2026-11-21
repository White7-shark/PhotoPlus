// Film & Collage (Photoshoot Studio).
//
// Film strip: photos laid out as frames on a strip of film, horizontal or
// vertical, in one of four styles (classic 35 mm, amber negative, photo booth,
// cinema). Film styles draw the base, the sprocket holes and the edge
// markings (the stock name and frame numbers, as real film carries them).
// Every frame can take its own transformation: one of ten looks, a zoom, a
// pan and a mirror, so one photo can fill a whole strip with variations.
//
// Collage: photos in cells (normalised rectangles chosen by the app), with
// the same per-cell transformations, a gap, an outer margin, rounded corners
// and a background colour.
//
// Photos are sampled bilinearly and cover-fitted into their frame, so any
// photo size and aspect works.
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "../portrait/common.hpp"
#include "photoplus/parallel.hpp"
#include "photoplus/profiler.hpp"
#include "photoplus/studio.hpp"

namespace photoplus::studio {

namespace {
using portrait::detail::offsets;

struct Rgbf {
  float r, g, b;
};

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float luma(const Rgbf& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
/** An S-curve around mid-grey: k > 0 adds contrast. */
inline float sCurve(float v, float k) { return clamp01(v + k * (v - 0.5f) * (1.0f - std::fabs(2.0f * v - 1.0f))); }
inline Rgbf saturate(const Rgbf& c, float s) {
  const float y = luma(c);
  return {y + (c.r - y) * s, y + (c.g - y) * s, y + (c.b - y) * s};
}

/** A deterministic grain value in -0.5..0.5. */
inline float grainAt(int32_t x, int32_t y, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h & 0xFFFF) / 65535.0f - 0.5f;
}

/** One of the frame looks, on a colour in gamma space (0..1). */
Rgbf applyLook(FrameLook look, Rgbf c, float vignette) {
  switch (look) {
    case FrameLook::Original:
      break;
    case FrameLook::BlackWhite: {
      const float y = sCurve(luma(c), 0.15f);
      c = {y, y, y};
      break;
    }
    case FrameLook::Sepia: {
      const float y = sCurve(luma(c), 0.1f);
      c = {clamp01(y * 1.07f + 0.03f), clamp01(y * 0.92f + 0.02f), clamp01(y * 0.72f + 0.02f)};
      break;
    }
    case FrameLook::Vintage: {
      c = saturate(c, 0.75f);
      c = {0.07f + 0.9f * c.r + 0.03f, 0.06f + 0.9f * c.g + 0.01f, 0.08f + 0.86f * c.b - 0.02f};
      break;
    }
    case FrameLook::Cool:
      c = saturate(c, 0.92f);
      c = {c.r * 0.95f, c.g * 0.99f + 0.01f, c.b * 1.04f + 0.035f};
      break;
    case FrameLook::Punch:
      c = saturate(c, 1.35f);
      c = {sCurve(clamp01(c.r), 0.25f), sCurve(clamp01(c.g), 0.25f), sCurve(clamp01(c.b), 0.25f)};
      break;
    case FrameLook::CrossProcess:
      c = {sCurve(clamp01(c.r), 0.35f), clamp01(sCurve(clamp01(c.g), 0.2f) * 1.03f), clamp01(0.2f + 0.6f * c.b)};
      c = saturate(c, 1.1f);
      break;
    case FrameLook::Faded:
      c = saturate(c, 0.85f);
      c = {0.12f + 0.82f * c.r, 0.12f + 0.82f * c.g, 0.13f + 0.8f * c.b};
      break;
    case FrameLook::Golden:
      c = saturate(c, 1.1f);
      c = {clamp01(c.r * 1.06f + 0.02f), clamp01(c.g * 1.0f + 0.01f), clamp01(c.b * 0.84f)};
      break;
    case FrameLook::Noir: {
      float y = sCurve(luma(c), 0.5f);
      y = clamp01((y - 0.04f) / 0.96f) * (1.0f - 0.45f * vignette);
      c = {y, y, y};
      break;
    }
  }
  return {clamp01(c.r), clamp01(c.g), clamp01(c.b)};
}

/** Bilinear sample of a photo in pixel coordinates (edge-clamped), gamma RGB 0..1. */
Rgbf sample(const ImageBuffer& img, float x, float y) {
  const int32_t w = img.width(), h = img.height();
  x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
  y = std::clamp(y, 0.0f, static_cast<float>(h - 1));
  const auto x0 = static_cast<int32_t>(x), y0 = static_cast<int32_t>(y);
  const int32_t x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
  const float tx = x - static_cast<float>(x0), ty = y - static_cast<float>(y0);
  const auto o = offsets(img);
  const uint8_t* a = img.row(y0) + x0 * 4;
  const uint8_t* b = img.row(y0) + x1 * 4;
  const uint8_t* c = img.row(y1) + x0 * 4;
  const uint8_t* d = img.row(y1) + x1 * 4;
  const auto ch = [&](int k) {
    const float top = static_cast<float>(a[k]) + (static_cast<float>(b[k]) - static_cast<float>(a[k])) * tx;
    const float bottom = static_cast<float>(c[k]) + (static_cast<float>(d[k]) - static_cast<float>(c[k])) * tx;
    return (top + (bottom - top) * ty) / 255.0f;
  };
  return {ch(o.r), ch(o.g), ch(o.b)};
}

struct Canvas {
  ImageBuffer& img;
  void put(int32_t x, int32_t y, const Rgbf& c, float a = 1.0f) {
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height() || a <= 0.0f) return;
    const auto o = offsets(img);
    uint8_t* p = img.row(y) + x * 4;
    const auto mix = [&](int k, float v) {
      p[k] = static_cast<uint8_t>(std::lround((static_cast<float>(p[k]) / 255.0f * (1.0f - a) + clamp01(v) * a) * 255.0f));
    };
    mix(o.r, c.r);
    mix(o.g, c.g);
    mix(o.b, c.b);
    p[3] = 255;
  }
  void fill(const Rgbf& c) {
    for (int32_t y = 0; y < img.height(); ++y)
      for (int32_t x = 0; x < img.width(); ++x) put(x, y, c);
  }
  /** An anti-aliased rounded rectangle [x0, x1) x [y0, y1). */
  void roundRect(float x0, float y0, float x1, float y1, float r, const Rgbf& c) {
    r = std::min({r, (x1 - x0) * 0.5f, (y1 - y0) * 0.5f});
    for (auto y = static_cast<int32_t>(std::floor(y0)); y < static_cast<int32_t>(std::ceil(y1)); ++y)
      for (auto x = static_cast<int32_t>(std::floor(x0)); x < static_cast<int32_t>(std::ceil(x1)); ++x) {
        const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
        const float dx = std::max({x0 + r - px, px - (x1 - r), 0.0f});
        const float dy = std::max({y0 + r - py, py - (y1 - r), 0.0f});
        const float inside = r > 0.0f ? r - std::hypot(dx, dy) : std::min({px - x0, x1 - px, py - y0, y1 - py});
        put(x, y, c, clamp01(inside + 0.5f));
      }
  }
};

// ---- a small 5x7 font for the film's edge markings ----
// Each glyph: 7 rows of 5 bits (most significant bit = left).
const std::array<uint8_t, 7>* glyph(char ch) {
  static const std::array<uint8_t, 7> kDigits[10] = {
      {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
      {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
      {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
  static const std::array<uint8_t, 7> kA{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static const std::array<uint8_t, 7> kH{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static const std::array<uint8_t, 7> kL{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
  static const std::array<uint8_t, 7> kO{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
  static const std::array<uint8_t, 7> kP{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
  static const std::array<uint8_t, 7> kS{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
  static const std::array<uint8_t, 7> kT{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
  static const std::array<uint8_t, 7> kU{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
  static const std::array<uint8_t, 7> kArrow{0x10, 0x18, 0x1C, 0x1E, 0x1C, 0x18, 0x10};  // '>' as a filled triangle
  if (ch >= '0' && ch <= '9') return &kDigits[ch - '0'];
  switch (ch) {
    case 'A': return &kA;
    case 'H': return &kH;
    case 'L': return &kL;
    case 'O': return &kO;
    case 'P': return &kP;
    case 'S': return &kS;
    case 'T': return &kT;
    case 'U': return &kU;
    case '>': return &kArrow;
    default: return nullptr;  // space and anything else
  }
}

/**
 * Draws `text` starting at (x, y) along the strip: horizontal text on a
 * horizontal strip; on a vertical strip it runs down the edge (rotated 90°),
 * as film markings do. `px` is one font pixel.
 */
void drawText(Canvas& cv, const std::string& text, float x, float y, float px, bool vertical, const Rgbf& c) {
  float pen = 0.0f;
  for (char ch : text) {
    if (const auto* g = glyph(ch)) {
      for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col) {
          if (((*g)[static_cast<size_t>(row)] >> (4 - col) & 1) == 0) continue;
          // Horizontal: col along x, row down y. Vertical: col down y, row to the left in x.
          const float gx = vertical ? x + static_cast<float>(6 - row) * px : x + (pen + static_cast<float>(col)) * px;
          const float gy = vertical ? y + (pen + static_cast<float>(col)) * px : y + static_cast<float>(row) * px;
          for (auto yy = static_cast<int32_t>(gy); yy < static_cast<int32_t>(std::ceil(gy + px)); ++yy)
            for (auto xx = static_cast<int32_t>(gx); xx < static_cast<int32_t>(std::ceil(gx + px)); ++xx) cv.put(xx, yy, c, 0.92f);
        }
    }
    pen += 6.0f;
  }
}

/** Length of `text` in font pixels. */
float textLength(const std::string& text) { return 6.0f * static_cast<float>(text.size()) - 1.0f; }

struct Box {
  float x0, y0, x1, y1;
  float w() const { return x1 - x0; }
  float h() const { return y1 - y0; }
};

/**
 * One photo drawn into `box` with its transformation: cover-fitted, zoomed
 * (1..4) around a pan point (-1..1 each way), optionally mirrored, then the
 * look; rounded corners (`radius` px) and a little grain on film.
 */
Error drawFrame(Canvas& cv, const ImageBuffer& photo, const Box& box, const FrameSpec& f, float radius, float grain, uint32_t seed,
                const CancellationToken* token) {
  const float pw = static_cast<float>(photo.width()), ph = static_cast<float>(photo.height());
  const float zoom = std::clamp(std::isfinite(f.zoom) ? f.zoom : 1.0f, 1.0f, 4.0f);
  const float cover = std::max(box.w() / pw, box.h() / ph) * zoom;
  // The part of the photo shown: centred on the face (when known), else a
  // little high (faces sit high in photos), then moved by the pan; always
  // inside the photo.
  const float viewW = box.w() / cover, viewH = box.h() / cover;
  const float panX = std::clamp(std::isfinite(f.panX) ? f.panX : 0.0f, -1.0f, 1.0f);
  const float panY = std::clamp(std::isfinite(f.panY) ? f.panY : 0.0f, -1.0f, 1.0f);
  const bool focused = f.focusX >= 0.0f && f.focusX <= 1.0f && f.focusY >= 0.0f && f.focusY <= 1.0f;
  const float baseX = focused ? f.focusX * pw : pw * 0.5f;
  const float baseY = focused ? f.focusY * ph : ph * 0.5f - 0.18f * (ph - viewH);
  const float cx = std::clamp(baseX + panX * (pw - viewW) * 0.5f, viewW * 0.5f, std::max(viewW * 0.5f, pw - viewW * 0.5f));
  const float cy = std::clamp(baseY + panY * (ph - viewH) * 0.5f, viewH * 0.5f, std::max(viewH * 0.5f, ph - viewH * 0.5f));
  const auto y0 = static_cast<int32_t>(std::floor(box.y0)), y1 = static_cast<int32_t>(std::ceil(box.y1));
  const bool done = parallelFor(y1 - y0, token, [&](int32_t begin, int32_t end) {
    for (int32_t yy = y0 + begin; yy < y0 + end; ++yy) {
      for (auto xx = static_cast<int32_t>(std::floor(box.x0)); xx < static_cast<int32_t>(std::ceil(box.x1)); ++xx) {
        const float px = static_cast<float>(xx) + 0.5f, py = static_cast<float>(yy) + 0.5f;
        float a = 1.0f;
        if (radius > 0.0f) {
          const float dx = std::max({box.x0 + radius - px, px - (box.x1 - radius), 0.0f});
          const float dy = std::max({box.y0 + radius - py, py - (box.y1 - radius), 0.0f});
          a = clamp01(radius - std::hypot(dx, dy) + 0.5f);
        }
        a = std::min({a, clamp01(px - box.x0 + 0.5f), clamp01(box.x1 - px + 0.5f), clamp01(py - box.y0 + 0.5f), clamp01(box.y1 - py + 0.5f)});
        if (a <= 0.0f) continue;
        float u = (px - box.x0) / box.w(), v = (py - box.y0) / box.h();
        if (f.flip) u = 1.0f - u;
        const Rgbf src = sample(photo, cx + (u - 0.5f) * viewW - 0.5f, cy + (v - 0.5f) * viewH - 0.5f);
        const float du = u - 0.5f, dv = v - 0.5f;
        Rgbf c = applyLook(f.look, src, clamp01((du * du + dv * dv) * 2.0f));
        if (grain > 0.0f) {
          const float g = grain * grainAt(xx, yy, seed);
          c = {clamp01(c.r + g), clamp01(c.g + g), clamp01(c.b + g)};
        }
        cv.put(xx, yy, c, a);
      }
    }
  });
  return done ? Error{PP_OK, {}} : cancelledError();
}

struct Style {
  Rgbf base;      // the film (or paper)
  Rgbf hole;      // what shows through a sprocket hole
  Rgbf marking;   // edge markings
  float aspect;   // frame width / height (landscape)
  float border;   // band each side, as a share of the frame's short edge
  float gap;      // between frames, as a share of the frame's short edge
  bool holes;
  float grain;
  float radius;   // frame corners, share of the short edge
};

Style styleOf(FilmStyle s) {
  switch (s) {
    case FilmStyle::Amber:  // a colour negative's orange base
      return {{0.60f, 0.33f, 0.15f}, {0.93f, 0.92f, 0.9f}, {0.98f, 0.86f, 0.55f}, 1.5f, 0.24f, 0.08f, true, 0.035f, 0.015f};
    case FilmStyle::PhotoBooth:  // a white paper strip of portraits
      return {{0.965f, 0.957f, 0.937f}, {0.965f, 0.957f, 0.937f}, {0.35f, 0.33f, 0.31f}, 0.8f, 0.09f, 0.09f, false, 0.0f, 0.0f};
    case FilmStyle::Cinema:  // wide frames on black, small holes
      return {{0.05f, 0.05f, 0.055f}, {0.88f, 0.88f, 0.88f}, {0.75f, 0.75f, 0.78f}, 2.39f, 0.2f, 0.06f, true, 0.03f, 0.0f};
    case FilmStyle::Classic:
    default:
      return {{0.08f, 0.08f, 0.085f}, {0.92f, 0.92f, 0.92f}, {0.95f, 0.66f, 0.25f}, 1.5f, 0.24f, 0.08f, true, 0.035f, 0.015f};
  }
}

bool validPhotos(const std::vector<const ImageBuffer*>& photos) {
  if (photos.empty() || photos.size() > kMaxFilmPhotos) return false;
  for (const auto* p : photos)
    if (p == nullptr || p->width() < 2 || p->height() < 2 ||
        (p->format() != PixelFormat::RGBA8 && p->format() != PixelFormat::BGRA8))
      return false;
  return true;
}
}  // namespace

Result<ImageBuffer> filmStrip(const std::vector<const ImageBuffer*>& photos, const std::vector<FrameSpec>& frames,
                              const FilmParams& params, const CancellationToken* token) {
  ScopedTimer timer("studio.film");
  if (!validPhotos(photos) || frames.empty() || frames.size() > kMaxFilmFrames || params.frameSize < 64 || params.frameSize > 2048)
    return failure(Error{PP_ERROR_INVALID_ARGUMENT, "A film strip needs 1 to 12 photos and frames"});
  for (const auto& f : frames)
    if (f.photo < 0 || static_cast<size_t>(f.photo) >= photos.size())
      return failure(Error{PP_ERROR_INVALID_ARGUMENT, "A frame names a photo that isn't there"});
  const Style st = styleOf(params.style);
  // A frame's size: its short edge is frameSize; portrait frames are taller than wide.
  const float shortE = static_cast<float>(params.frameSize);
  const float wide = std::max(st.aspect, 1.0f / st.aspect);
  const float aspect = params.portraitFrames ? 1.0f / wide : wide;
  const float fw = aspect >= 1.0f ? shortE * aspect : shortE, fh = aspect >= 1.0f ? shortE : shortE / aspect;
  const bool vertical = params.vertical;
  const float along = vertical ? fh : fw;    // a frame's length along the strip
  const float across = vertical ? fw : fh;   // and across it
  const float border = st.border * shortE;
  const float gap = st.gap * shortE;
  const auto n = static_cast<float>(frames.size());
  const float lenAlong = n * along + (n + 1.0f) * gap + (st.holes ? 0.0f : border);  // booth strips have a deeper foot
  const float lenAcross = across + 2.0f * border;
  const auto W = static_cast<int32_t>(std::lround(vertical ? lenAcross : lenAlong));
  const auto H = static_cast<int32_t>(std::lround(vertical ? lenAlong : lenAcross));
  if (W > 16384 || H > 16384) return failure(Error{PP_ERROR_INVALID_ARGUMENT, "The film strip would be too large"});
  auto out = ImageBuffer::allocate(W, H, PixelFormat::RGBA8);
  if (!out) return out;
  Canvas cv{*out};
  cv.fill(st.base);
  // Maps (along, across) on the strip to a box in the image.
  const auto box = [&](float a0, float a1, float c0, float c1) {
    return vertical ? Box{c0, a0, c1, a1} : Box{a0, c0, a1, c1};
  };
  // Sprocket holes along both edges, at film's own pitch.
  if (st.holes) {
    const float holeAlong = 0.07f * across, holeAcross = 0.28f * border;
    const float pitch = 0.16f * across;
    const float r = 0.22f * std::min(holeAlong, holeAcross);
    for (float a = pitch * 0.5f; a + holeAlong < lenAlong; a += pitch)
      for (int side = 0; side < 2; ++side) {
        const float c0 = side == 0 ? 0.1f * border : lenAcross - 0.1f * border - holeAcross;
        const Box b = box(a, a + holeAlong, c0, c0 + holeAcross);
        cv.roundRect(b.x0, b.y0, b.x1, b.y1, r, st.hole);
      }
  }
  // The frames.
  for (size_t i = 0; i < frames.size(); ++i) {
    const float a0 = gap + static_cast<float>(i) * (along + gap);
    const Box b = box(a0, a0 + along, border, border + across);
    const uint32_t seed = params.seed + static_cast<uint32_t>(i) * 7919u;
    if (Error e = drawFrame(cv, *photos[static_cast<size_t>(frames[i].photo)], b, frames[i], st.radius * shortE, st.grain, seed, token);
        e.status != PP_OK)
      return failure(e);
  }
  // Edge markings: the stock name on one edge, frame numbers on the other
  // (between the holes and the frame), as real film carries them.
  if (params.markings) {
    if (st.holes) {
      const std::string name = params.style == FilmStyle::Cinema ? "PHOTO PLUS" : "PHOTO PLUS 400";
      // One font pixel: the text sits in the band between the holes and the
      // frame, and is never longer than a frame.
      const float px = std::max(1.0f, std::min(0.045f * border, 0.85f * along / textLength(name)));
      const float c0 = 0.7f * border - 3.5f * px;
      for (size_t i = 0; i < frames.size(); ++i) {
        const float a0 = gap + static_cast<float>(i) * (along + gap);
        const std::string num = std::to_string(i + 1) + "  >" + std::to_string(i + 1) + "A";
        const float startName = a0 + 0.5f * (along - textLength(name) * px);
        const float startNum = a0 + 0.5f * (along - textLength(num) * px);
        const float c1 = lenAcross - 0.7f * border - 3.5f * px;
        if (vertical) {
          drawText(cv, name, c0, startName, px, true, st.marking);
          drawText(cv, num, c1, startNum, px, true, st.marking);
        } else {
          drawText(cv, name, startName, c0, px, false, st.marking);
          drawText(cv, num, startNum, c1, px, false, st.marking);
        }
      }
    } else {
      // A booth strip carries its name in the deeper foot.
      const std::string name = "PHOTO PLUS";
      const float p2 = std::max(1.0f, 0.09f * border);
      const float a = lenAlong - 0.5f * (border + gap) - 3.5f * p2;
      if (vertical) drawText(cv, name, 0.5f * (lenAcross - textLength(name) * p2), a, p2, false, st.marking);
      else drawText(cv, name, a, 0.5f * (lenAcross - textLength(name) * p2), p2, true, st.marking);
    }
  }
  return out;
}

Result<ImageBuffer> collage(const std::vector<const ImageBuffer*>& photos, const std::vector<CollageCell>& cells,
                            const CollageParams& params, const CancellationToken* token) {
  ScopedTimer timer("studio.collage");
  if (!validPhotos(photos) || cells.empty() || cells.size() > kMaxFilmFrames || params.width < 64 || params.height < 64 ||
      params.width > 8192 || params.height > 8192)
    return failure(Error{PP_ERROR_INVALID_ARGUMENT, "A collage needs 1 to 12 photos and cells"});
  for (const auto& c : cells) {
    if (c.frame.photo < 0 || static_cast<size_t>(c.frame.photo) >= photos.size())
      return failure(Error{PP_ERROR_INVALID_ARGUMENT, "A cell names a photo that isn't there"});
    if (!(c.x >= 0.0f && c.y >= 0.0f && c.w > 0.0f && c.h > 0.0f && c.x + c.w <= 1.0001f && c.y + c.h <= 1.0001f))
      return failure(Error{PP_ERROR_INVALID_ARGUMENT, "A cell lies outside the collage"});
  }
  auto out = ImageBuffer::allocate(params.width, params.height, PixelFormat::RGBA8);
  if (!out) return out;
  Canvas cv{*out};
  cv.fill({params.background[0] / 255.0f, params.background[1] / 255.0f, params.background[2] / 255.0f});
  const float W = static_cast<float>(params.width), H = static_cast<float>(params.height);
  const float shortSide = std::min(W, H);
  const float margin = std::clamp(std::isfinite(params.margin) ? params.margin : 0.0f, 0.0f, 0.2f) * shortSide;
  const float gap = std::clamp(std::isfinite(params.gap) ? params.gap : 0.0f, 0.0f, 0.1f) * shortSide;
  const float innerW = W - 2.0f * margin, innerH = H - 2.0f * margin;
  for (size_t i = 0; i < cells.size(); ++i) {
    const auto& c = cells[i];
    Box b{margin + c.x * innerW + 0.5f * gap, margin + c.y * innerH + 0.5f * gap, margin + (c.x + c.w) * innerW - 0.5f * gap,
          margin + (c.y + c.h) * innerH - 0.5f * gap};
    if (b.w() < 2.0f || b.h() < 2.0f) continue;
    const float radius = std::clamp(std::isfinite(params.radius) ? params.radius : 0.0f, 0.0f, 0.5f) * std::min(b.w(), b.h());
    if (Error e = drawFrame(cv, *photos[static_cast<size_t>(c.frame.photo)], b, c.frame, radius, 0.0f, 0, token); e.status != PP_OK)
      return failure(e);
  }
  return out;
}

}  // namespace photoplus::studio
