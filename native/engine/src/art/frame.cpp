// Photo Arts: museum frames around a finished painting. Each frame is a
// moulding profile lit from the upper left (the mitred corners catch the
// light differently on each side), in its material — gold leaf with its
// worn high points, satin black, walnut with its grain along the length, a
// white mat with its bevelled cut — and the frame's own shadow falls on the
// painting's edge.

#include <algorithm>
#include <cmath>

#include "core.hpp"
#include "photoplus/art.hpp"
#include "photoplus/parallel.hpp"

namespace photoplus::art {

using namespace core;

namespace {

struct Style {
  float frame;   // the moulding's width, share of the painting's short side
  float mat;     // a mat inside it (0: none)
  Rgb3 dark, light;
  float spec;    // shine
  int kind;      // 0 gilded, 1 baroque, 2 black, 3 walnut, 4 light oak around a mat
};

Style styleOf(Frame f) {
  switch (f) {
    case Frame::Gilded: return {0.085f, 0, hex(0x5E3F12), hex(0xF2D37C), 0.55f, 0};
    case Frame::Baroque: return {0.13f, 0, hex(0x4E330E), hex(0xF5D98A), 0.6f, 1};
    case Frame::BlackGallery: return {0.05f, 0, hex(0x0B0B0C), hex(0x3A3A3D), 0.25f, 2};
    case Frame::Walnut: return {0.075f, 0, hex(0x2E1B10), hex(0x7A5234), 0.18f, 3};
    case Frame::WhiteMat: return {0.032f, 0.09f, hex(0x8A6E4E), hex(0xD8C2A0), 0.15f, 4};
    default: return {0, 0, {}, {}, 0, 0};
  }
}

// Height of the moulding across its width, t 0 (outer edge) .. 1 (inner edge), and along it (u, px) for ornament.
float profile(int kind, float t, float u, float period, uint32_t seed) {
  switch (kind) {
    case 0: {  // a gilded cassetta: a raised outer lip, a cove, a bead at the sight edge
      const float lip = 0.55f * smoothstep(0.0f, 0.22f, t) * (1 - smoothstep(0.38f, 0.72f, t));
      const float bead = std::max(0.0f, 1 - std::pow((t - 0.88f) / 0.07f, 2.0f));
      return 0.2f + lip + 0.3f * std::sqrt(bead);
    }
    case 1: {  // baroque: a deep ogee with carved leaves and a run of beads
      float h = 0.7f * std::sin(kPi * std::pow(t, 0.8f));
      const float band = smoothstep(0.22f, 0.32f, t) * (1 - smoothstep(0.62f, 0.72f, t));
      const float leaf = std::pow(std::max(0.0f, std::sin(2 * kPi * u / period)), 2.0f) * (0.6f + 0.4f * std::cos(kPi * (t - 0.47f) / 0.25f));
      h += 0.45f * band * leaf + 0.08f * band * (valueNoise(u / (period * 0.3f), t * 8.0f, seed) - 0.5f);
      const float beadT = std::max(0.0f, 1 - std::pow((t - 0.86f) / 0.06f, 2.0f));
      const float beadU = std::max(0.0f, std::cos(2 * kPi * u / (period * 0.25f)));
      return h + 0.3f * std::sqrt(beadT) * (0.5f + 0.5f * beadU);
    }
    case 2:  // a gallery frame: flat face, small rounded edges
      return 0.5f * smoothstep(0.0f, 0.12f, t) * (1 - smoothstep(0.88f, 1.0f, t));
    case 3: {  // walnut: an ogee
      return 0.35f + 0.35f * std::sin(kPi * t) - 0.15f * std::sin(2 * kPi * t);
    }
    default:  // a slim oak frame
      return 0.5f * std::sin(kPi * t);
  }
}

}  // namespace

Error frame(const ImageBuffer& painting, Frame style, ImageBuffer& out, const CancellationToken* token) {
  if (painting.empty() || (painting.format() != PixelFormat::RGBA8 && painting.format() != PixelFormat::BGRA8))
    return {PP_ERROR_INVALID_ARGUMENT, "A frame needs an RGBA or BGRA painting"};
  const int fi = static_cast<int>(style);
  if (fi <= 0 || fi >= kFrameCount) return {PP_ERROR_INVALID_ARGUMENT, "Unknown frame"};
  const Style st = styleOf(style);
  const int W = painting.width(), H = painting.height();
  const float S = static_cast<float>(std::min(W, H));
  const int fw = std::max(4, static_cast<int>(st.frame * S + 0.5f));
  const int mw = st.mat > 0 ? std::max(4, static_cast<int>(st.mat * S + 0.5f)) : 0;
  const int b = fw + mw;
  const int OW = W + 2 * b, OH = H + 2 * b;
  auto made = ImageBuffer::allocate(OW, OH, painting.format());
  if (!made) return made.error();
  out = std::move(made).value();
  const bool bgra = painting.format() == PixelFormat::BGRA8;
  const uint32_t seed = 0x5EED5u + static_cast<uint32_t>(fi);
  const float period = std::max(12.0f, 0.75f * static_cast<float>(fw));
  // Light from the upper left, a little in front.
  const float lx = -0.55f, ly = -0.55f, lz = 0.63f;
  const float hx = lx, hy = ly, hz = lz + 1.0f, hl = std::sqrt(hx * hx + hy * hy + hz * hz);
  const Rgb3 matCol = hex(0xF2EFE6), core = hex(0xFBFAF6);

  if (!parallelFor(OH, token, [&](int32_t y0, int32_t y1) {
        for (int y = y0; y < y1; ++y) {
          uint8_t* row = out.row(y);
          for (int x = 0; x < OW; ++x) {
            float col[3];
            const int px = x - b, py = y - b;
            if (px >= 0 && py >= 0 && px < W && py < H) {
              const uint8_t* src = painting.row(py) + static_cast<size_t>(px) * 4;
              for (int k = 0; k < 3; ++k) col[k] = static_cast<float>(src[bgra ? 2 - k : k]) / 255.0f;
              // The frame's shadow on the painting: deeper below the top and right of the left edge.
              const float sd = std::max(1.5f, 0.007f * S);
              const float sh = 0.35f * std::exp(-static_cast<float>(py) / sd) + 0.3f * std::exp(-static_cast<float>(px) / sd) +
                               0.08f * std::exp(-static_cast<float>(H - 1 - py) / sd) + 0.08f * std::exp(-static_cast<float>(W - 1 - px) / sd);
              for (float& v : col) v *= 1 - std::min(0.45f, sh);
            } else {
              // Which side (mitred at the corners), how far in from the outer edge, and where along it.
              const int dl = x, dr = OW - 1 - x, dt = y, db = OH - 1 - y;
              const int d = std::min({dl, dr, dt, db});
              float inx = 0, iny = 0, u = 0;
              if (d == dt) iny = 1, u = static_cast<float>(x);
              else if (d == db) iny = -1, u = static_cast<float>(x);
              else if (d == dl) inx = 1, u = static_cast<float>(y);
              else inx = -1, u = static_cast<float>(y);
              if (d >= fw) {
                // The mat, with its bevelled cut catching the light at the sight edge.
                const float toSight = static_cast<float>(b - d);
                const float bevel = std::max(2.0f, 0.006f * S);
                for (int k = 0; k < 3; ++k) col[k] = matCol[static_cast<size_t>(k)];
                if (toSight <= bevel) {
                  const float lit = 0.85f + 0.25f * -(inx * lx + iny * ly);
                  for (int k = 0; k < 3; ++k) col[k] = std::min(1.0f, core[static_cast<size_t>(k)] * lit);
                }
                const float n = 0.97f + 0.03f * valueNoise(static_cast<float>(x) * 0.4f, static_cast<float>(y) * 0.4f, seed);
                for (float& v : col) v *= n;
                // The frame's shadow on the mat.
                const float sd = std::max(2.0f, 0.01f * S);
                const float o = static_cast<float>(d - fw);
                const float sh = (iny > 0 || inx > 0 ? 0.35f : 0.1f) * std::exp(-o / sd);
                for (float& v : col) v *= 1 - sh;
              } else {
                const float t = (static_cast<float>(d) + 0.5f) / static_cast<float>(fw);
                const float dtau = 1.0f / static_cast<float>(fw);
                const float h0 = profile(st.kind, t, u, period, seed);
                const float dh = (profile(st.kind, std::min(1.0f, t + dtau), u, period, seed) - profile(st.kind, std::max(0.0f, t - dtau), u, period, seed)) /
                                 (2 * dtau);
                const float du = (profile(st.kind, t, u + 1, period, seed) - profile(st.kind, t, u - 1, period, seed)) * 0.5f * static_cast<float>(fw);
                // The surface normal: the profile's slope runs inwards, the ornament's along the side.
                const float ax = inx != 0 ? 0.0f : 1.0f, ay = inx != 0 ? 1.0f : 0.0f;  // along the side
                float nx = -(dh * 0.35f) * inx - du * 0.35f * ax, ny = -(dh * 0.35f) * iny - du * 0.35f * ay, nz = 1;
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= nl, ny /= nl, nz /= nl;
                const float diff = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                const float spec = std::pow(std::max(0.0f, (nx * hx + ny * hy + nz * hz) / hl), st.kind <= 1 ? 18.0f : 28.0f) * st.spec;
                const float shade = std::clamp(0.25f + 0.85f * diff + 0.15f * h0, 0.0f, 1.2f);
                Rgb3 base{};
                // Metal: its tone falls off quickly away from the light (deep in the coves, bright on the crests).
                const float m = st.kind <= 1 ? std::pow(std::min(1.0f, shade), 1.8f) : std::min(1.0f, shade);
                for (size_t k = 0; k < 3; ++k) base[k] = mix(st.dark[k], st.light[k], m);
                if (st.kind == 0 || st.kind == 1) {
                  // Gold leaf worn at the high points (the red bole shows), dark in the recesses.
                  const float wear = smoothstep(0.75f, 0.95f, valueNoise(u / (period * 0.6f), t * 6.0f, seed + 3U)) * smoothstep(0.4f, 0.8f, h0);
                  const Rgb3 bole = hex(0x7A3A22);
                  for (size_t k = 0; k < 3; ++k) base[k] = mix(base[k], bole[k] * shade, 0.35f * wear);
                } else if (st.kind == 3 || st.kind == 4) {
                  // Wood grain running along the moulding.
                  const float g = fbm(u / (period * 2.5f), t * 3.0f + static_cast<float>(d) * 0.05f, seed + 5U, 4);
                  const float rings = 0.5f + 0.5f * std::sin(g * 18.0f);
                  for (size_t k = 0; k < 3; ++k) base[k] *= 0.82f + 0.25f * rings;
                }
                for (size_t k = 0; k < 3; ++k) col[k] = clamp01(base[k] + spec * (st.kind <= 1 ? 1.0f : 0.6f));
                // The outer edge and the sight edge fall away into shadow.
                const float edge = smoothstep(0.0f, 0.04f, t) * (st.mat > 0 ? 1.0f : 0.55f + 0.45f * (1 - smoothstep(0.96f, 1.0f, t)));
                for (float& v : col) v *= 0.35f + 0.65f * edge;
              }
            }
            uint8_t* dst = row + static_cast<size_t>(x) * 4;
            for (int k = 0; k < 3; ++k) dst[bgra ? 2 - k : k] = static_cast<uint8_t>(std::lround(clamp01(col[k]) * 255.0f));
            dst[3] = 255;
          }
        }
      }))
    return {PP_ERROR_CANCELLED, "Cancelled"};
  return {PP_OK, {}};
}

}  // namespace photoplus::art
