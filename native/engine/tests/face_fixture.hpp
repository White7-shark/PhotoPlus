#pragma once

// A synthetic head-and-shoulders photo with landmarks (six skin tones, deep
// to light), shared by the face and beauty tests.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "photoplus/pp_engine.h"

namespace fixture {

struct Rgbf {
  float r, g, b;
};

inline float hash01(int x, int y, unsigned seed) {
  auto h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}

inline float ell(float x, float y, float cx, float cy, float rx, float ry) {
  const float dx = (x - cx) / rx, dy = (y - cy) / ry;
  return dx * dx + dy * dy;
}

// Six skin tones from deep to light (sRGB, roughly the Monk scale's spread).
inline const std::array<Rgbf, 6> kTones{{{0.27f, 0.17f, 0.12f},
                                  {0.40f, 0.26f, 0.18f},
                                  {0.56f, 0.38f, 0.27f},
                                  {0.71f, 0.50f, 0.37f},
                                  {0.84f, 0.65f, 0.53f},
                                  {0.95f, 0.80f, 0.71f}}};

struct FaceOptions {
  bool mouthOpen = true;
  bool shine = true;
  bool darkCircles = true;
  bool sideShadow = false;
  bool backlit = false;
  bool whiteWall = false;  // a bright background, but the face is lit from the front
};

// A head-and-shoulders photo in a 240x300 design space scaled by k, with
// landmarks in the layout of pp_face. Features: eyes (whites, iris, pupil),
// brows, lips around an open mouth with yellowish teeth, dark circles,
// a shine on the forehead and nose, optional side shadow and backlight.
struct Face {
  int k, w, h;
  float cx = 120, cy = 130, rx = 52, ry = 68;  // design units
  std::vector<Rgbf> px;
  std::vector<uint8_t> region;  // 1 face, 2 eye white, 3 iris, 4 teeth, 5 lips, 6 under-eye, 7 shine, 8 cheek, 9 background
  pp_face lm{};

  Face(Rgbf skin, FaceOptions o, int scale = 2, unsigned seed = 3) : k(scale), w(240 * scale), h(300 * scale) {
    px.resize(static_cast<size_t>(w) * h);
    region.assign(px.size(), 0);
    const Rgbf bg = (o.backlit || o.whiteWall) ? Rgbf{0.92f, 0.94f, 0.97f} : Rgbf{0.32f, 0.42f, 0.36f};
    const float light = o.backlit ? 0.45f : 1.0f;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(k), fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(k);
        const size_t i = static_cast<size_t>(y) * w + x;
        const float tex = 0.04f * (hash01(x / k, y / k, seed) - 0.5f);
        Rgbf c{bg.r + tex, bg.g + tex, bg.b + tex};
        uint8_t reg = 9;
        const bool neck = fx > 98 && fx < 142 && fy > 180;
        const float e = ell(fx, fy, cx, cy, rx, ry);
        if (e <= 1.0f || neck) {
          const float pore = 0.05f * (hash01(x, y, seed + 7) - 0.5f);
          float l = light * (1.0f + pore);
          if (o.sideShadow) l *= 0.62f + 0.38f * std::clamp((fx - (cx - rx)) / (2.0f * rx), 0.0f, 1.0f);
          c = {skin.r * l, skin.g * l, skin.b * l};
          reg = e <= 1.0f ? 1 : 0;
          // Cheeks (measurement areas): beside the nose, below the eye bags.
          if (ell(fx, fy, cx - 26, cy + 16, 9, 6) <= 1 || ell(fx, fy, cx + 26, cy + 16, 9, 6) <= 1) reg = 8;
          // Dark circles under both eyes.
          for (const float exx : {cx - 22, cx + 22}) {
            if (ell(fx, fy, exx, cy - 2, 11, 4.5f) <= 1.0f) {
              reg = 6;
              if (o.darkCircles) c = {c.r * 0.78f, c.g * 0.76f, c.b * 0.80f};
            }
          }
          // Shine: a soft specular patch on the forehead and the nose tip.
          const float s = std::max(std::exp(-ell(fx, fy, cx, cy - 40, 14, 7)), std::exp(-ell(fx, fy, cx, cy + 12, 5, 4)));
          if (o.shine && s > 0.05f) {
            const float m = 0.55f * s;
            c = {c.r + (1.0f - c.r) * m, c.g + (1.0f - c.g) * m, c.b + (1.0f - c.b) * m};
            if (s > 0.5f) reg = 7;
          }
          if (e <= 1.0f) {
            for (const float exx : {cx - 22, cx + 22}) {
              const float ee = ell(fx, fy, exx, cy - 12, 11, 5);
              if (ee <= 1.0f) {
                const float d = std::hypot(fx - exx, fy - (cy - 12));
                if (d < 1.8f) {
                  c = {0.03f, 0.03f, 0.03f};
                  reg = 3;
                } else if (d < 4.2f) {
                  const float rings = 0.06f * std::sin(d * 6.0f);
                  c = {0.30f + rings, 0.19f + rings, 0.10f + rings};
                  reg = 3;
                } else {
                  c = {0.86f, 0.80f, 0.78f};  // slightly red, dull eye whites
                  reg = 2;
                }
              }
              if (ell(fx, fy, exx, cy - 25, 13, 2.5f) <= 1.0f) c = {0.12f, 0.08f, 0.06f};
            }
            // Lips with an open mouth (teeth) or a closed one.
            const float lipE = ell(fx, fy, cx, cy + 38, 19, 9);
            if (lipE <= 1.0f) {
              c = {0.62f * std::max(0.55f, skin.r + 0.05f), 0.24f * std::max(0.5f, skin.g + 0.1f), 0.27f * std::max(0.5f, skin.b + 0.1f)};
              reg = 5;
              if (o.mouthOpen && ell(fx, fy, cx, cy + 38, 14, 4.5f) <= 1.0f) {
                c = fy < cy + 39 ? Rgbf{0.86f, 0.80f, 0.62f} : Rgbf{0.25f, 0.08f, 0.08f};
                reg = fy < cy + 39 ? 4 : 0;
              }
            }
          }
        }
        px[i] = c;
        region[i] = reg;
      }
    landmarks(o.mouthOpen);
  }

  void put(int idx, float x, float y) {
    lm.points[idx * 2] = x / 240.0f;
    lm.points[idx * 2 + 1] = y / 300.0f;
  }
  void arc(int first, int n, float ecx, float ecy, float erx, float ery, float t0, float t1) {
    for (int i = 0; i < n; ++i) {
      const float t = t0 + (t1 - t0) * static_cast<float>(i) / static_cast<float>(n - 1);
      put(first + i, ecx + erx * std::cos(t), ecy + ery * std::sin(t));
    }
  }
  void landmarks(bool open) {
    const float pi = 3.14159265f;
    for (int i = 0; i < 36; ++i) {
      const float t = -pi / 2 + 2 * pi * static_cast<float>(i) / 36.0f;  // from the top, clockwise
      put(PP_FACE_OVAL + i, cx + rx * std::cos(t), cy + ry * std::sin(t));
    }
    const float exL = cx - 22, exR = cx + 22, ey = cy - 12;
    arc(PP_FACE_LEFT_BROW_TOP, 5, exL, cy - 25, 13, 2.5f, pi, 2 * pi);
    arc(PP_FACE_LEFT_BROW_BOTTOM, 5, exL, cy - 25, 13, 2.5f, pi, 0);
    arc(PP_FACE_RIGHT_BROW_TOP, 5, exR, cy - 25, 13, 2.5f, pi, 2 * pi);
    arc(PP_FACE_RIGHT_BROW_BOTTOM, 5, exR, cy - 25, 13, 2.5f, pi, 0);
    for (int i = 0; i < 16; ++i) {
      const float t = 2 * pi * static_cast<float>(i) / 16.0f;
      put(PP_FACE_LEFT_EYE + i, exL + 11 * std::cos(t), ey + 5 * std::sin(t));
      put(PP_FACE_RIGHT_EYE + i, exR + 11 * std::cos(t), ey + 5 * std::sin(t));
    }
    const float my = cy + 38;
    arc(PP_FACE_UPPER_LIP_TOP, 11, cx, my, 19, 9, pi, 2 * pi);
    arc(PP_FACE_LOWER_LIP_BOTTOM, 9, cx, my, 19, 9, pi * 0.9f, pi * 0.1f);
    if (open) {
      arc(PP_FACE_UPPER_LIP_BOTTOM, 9, cx, my, 14, 4.5f, pi * 1.1f, pi * 1.9f);
      arc(PP_FACE_LOWER_LIP_TOP, 9, cx, my, 14, 4.5f, pi * 0.9f, pi * 0.1f);
    } else {
      arc(PP_FACE_UPPER_LIP_BOTTOM, 9, cx, my, 14, 0.4f, pi * 1.1f, pi * 1.9f);
      arc(PP_FACE_LOWER_LIP_TOP, 9, cx, my, 14, 0.4f, pi * 0.9f, pi * 0.1f);
    }
    put(PP_FACE_NOSE_BRIDGE, cx, cy - 10);
    put(PP_FACE_NOSE_BRIDGE + 1, cx, cy + 8);
    put(PP_FACE_NOSE_BOTTOM, cx - 7, cy + 14);
    put(PP_FACE_NOSE_BOTTOM + 1, cx, cy + 16);
    put(PP_FACE_NOSE_BOTTOM + 2, cx + 7, cy + 14);
    lm.detailed = 1;
  }

  pp_image* image() const {
    pp_image* img = nullptr;
    pp_image_create(w, h, PP_FORMAT_RGBA8, &img);
    auto* p = static_cast<uint8_t*>(pp_image_pixels(img));
    for (size_t i = 0; i < px.size(); ++i) {
      p[i * 4 + 0] = static_cast<uint8_t>(std::lround(std::clamp(px[i].r, 0.0f, 1.0f) * 255));
      p[i * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(px[i].g, 0.0f, 1.0f) * 255));
      p[i * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(px[i].b, 0.0f, 1.0f) * 255));
      p[i * 4 + 3] = 255;
    }
    return img;
  }
};

struct Px {
  float y, cb, cr;
};
inline Px ycc(const uint8_t* q) {
  const float r = q[0] / 255.0f, g = q[1] / 255.0f, b = q[2] / 255.0f;
  return {0.299f * r + 0.587f * g + 0.114f * b, -0.168736f * r - 0.331264f * g + 0.5f * b, 0.5f * r - 0.418688f * g - 0.081312f * b};
}

// Mean Y, Cb, Cr over a region.
inline Px meanOf(pp_image* img, const Face& f, uint8_t reg) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  double y = 0, cb = 0, cr = 0;
  size_t n = 0;
  for (int yy = 0; yy < f.h; ++yy)
    for (int xx = 0; xx < f.w; ++xx)
      if (f.region[static_cast<size_t>(yy) * f.w + xx] == reg) {
        const Px v = ycc(p + static_cast<size_t>(yy) * info.stride + xx * 4);
        y += v.y, cb += v.cb, cr += v.cr, ++n;
      }
  const double d = n ? static_cast<double>(n) : 1.0;
  return {static_cast<float>(y / d), static_cast<float>(cb / d), static_cast<float>(cr / d)};
}

// Mean absolute difference (0..255 levels) over pixels of `reg` (0 = all).
inline double diffOver(pp_image* a, pp_image* b, const Face& f, int reg) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(a));
  const auto* q = static_cast<const uint8_t*>(pp_image_pixels(b));
  pp_image_info info{};
  pp_image_get_info(a, &info);
  double s = 0;
  size_t n = 0;
  for (int yy = 0; yy < f.h; ++yy)
    for (int xx = 0; xx < f.w; ++xx)
      if (reg == 0 || f.region[static_cast<size_t>(yy) * f.w + xx] == reg) {
        const size_t o = static_cast<size_t>(yy) * info.stride + xx * 4;
        for (int c = 0; c < 3; ++c) s += std::abs(p[o + c] - q[o + c]);
        n += 3;
      }
  return n ? s / static_cast<double>(n) : 0.0;
}

// Fine texture (pixel-level) over a region: what reads as real skin.
inline double fineTexture(pp_image* img, const Face& f, uint8_t reg) {
  const auto* p = static_cast<const uint8_t*>(pp_image_pixels(img));
  pp_image_info info{};
  pp_image_get_info(img, &info);
  double s = 0;
  size_t n = 0;
  for (int yy = 1; yy < f.h - 1; ++yy)
    for (int xx = 1; xx < f.w - 1; ++xx)
      if (f.region[static_cast<size_t>(yy) * f.w + xx] == reg) {
        auto Y = [&](int x, int y) { return ycc(p + static_cast<size_t>(y) * info.stride + x * 4).y; };
        s += std::fabs(Y(xx, yy) - 0.25f * (Y(xx - 1, yy) + Y(xx + 1, yy) + Y(xx, yy - 1) + Y(xx, yy + 1)));
        ++n;
      }
  return n ? s / static_cast<double>(n) : 0.0;
}

inline float hueDeg(const Px& v) { return std::atan2(v.cr, v.cb) * 57.29578f; }

}  // namespace fixture
