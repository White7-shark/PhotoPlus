// Portrait: subject stays crisp, background melts into a lens-like blur,
// faces get natural retouching.
//
//   coarse subject mask ─▶ edge-aligned matte (fast guided filter)
//   face areas ─▶ skin model calibrated to THIS person's own colour ─▶
//     spot healing (normalised-convolution inpainting) ─▶ edge-preserving
//     smoothing that keeps a share of real texture ─▶ blotch evening toward
//     the person's own median tone ─▶ gentle exposure-style lift
//   subject ─▶ clarity and vibrance (not on skin)
//   background ─▶ disc ("bokeh") blur in linear light with highlight bloom,
//     subject-free (normalised convolution, so no halo of subject colour),
//     matched grain so blurred areas don't look plastic
//   composite in linear light along the matte.
//
// Skin colour is never pushed toward a different tone: chroma only moves
// toward the person's own measured median, and brightening is a luminance
// lift that preserves hue. Tests check this across skin tones.

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "common.hpp"
#include "photoplus/enhance.hpp"
#include "photoplus/portrait.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::portrait {

using namespace detail;

namespace {

// Gaussian blur that stays fast for large sigmas: blur on a reduced grid,
// then upsample (the result is smooth by construction).
Result<Plane> blurLarge(const Plane& src, float sigma, const CancellationToken* token) {
  const int32_t f = std::max<int32_t>(1, static_cast<int32_t>(sigma / 3.0f));
  if (f == 1) {
    auto out = Plane::allocate(src.width(), src.height());
    if (!out) return failure(out.error());
    if (Error e = gaussianBlur(src, *out, sigma, token); e.status != PP_OK) return failure(e);
    return out;
  }
  auto low = boxDown(src, f, token);
  if (!low) return failure(low.error());
  auto lowBlur = Plane::allocate(low->width(), low->height());
  if (!lowBlur) return failure(lowBlur.error());
  if (Error e = gaussianBlur(*low, *lowBlur, sigma / static_cast<float>(f), token); e.status != PP_OK) return failure(e);
  // Upsample aligned to the box grid.
  auto out = Plane::allocate(src.width(), src.height());
  if (!out) return failure(out.error());
  const float inv = 1.0f / static_cast<float>(f);
  const bool done = parallelFor(src.height(), token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      float* o = out->row(y);
      const float fy = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
      for (int32_t x = 0; x < src.width(); ++x) o[x] = lowBlur->sample((static_cast<float>(x) + 0.5f) * inv - 0.5f, fy);
    }
  });
  if (!done) return failure(cancelledError());
  return out;
}

struct Rect {
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  int32_t w() const { return x1 - x0; }
  int32_t h() const { return y1 - y0; }
  bool contains(int32_t x, int32_t y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

float median(std::vector<float>& v) {
  if (v.empty()) return 0.0f;
  auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
  std::nth_element(v.begin(), mid, v.end());
  return *mid;
}

// Result of retouching, on a reduced grid covering `rect` (full-res pixels):
// the composite applies it with frequency separation, so the photo itself
// is never copied into full-size working planes.
struct SkinResult {
  Rect rect;
  int32_t scale = 1;      // full-res pixels per grid cell
  Plane skin;             // skin weight 0..1
  Plane base, retouched;  // luma before / after retouching (grid)
  Plane dcb, dcr;         // chroma change (grid)
  float keep = 1.0f;      // share of fine texture kept on skin
  int32_t spots = 0;
  double skinSum = 0.0;   // in full-res pixels

  bool active() const { return !skin.empty(); }
};

// Blemish-sized clusters only (stray pixels are not reported as spots).
int32_t countSpots(const Plane& spot, int32_t minArea) {
  const int32_t w = spot.width(), h = spot.height();
  std::vector<uint8_t> seen(spot.size(), 0);
  std::vector<int32_t> stack;
  int32_t count = 0;
  for (int32_t i = 0; i < w * h; ++i) {
    if (seen[static_cast<size_t>(i)] || spot.data()[i] < 0.5f) continue;
    int32_t area = 0;
    stack.push_back(i);
    while (!stack.empty()) {
      const int32_t j = stack.back();
      stack.pop_back();
      if (seen[static_cast<size_t>(j)] || spot.data()[j] < 0.5f) continue;
      seen[static_cast<size_t>(j)] = 1;
      ++area;
      const int32_t x = j % w, y = j / w;
      if (x > 0) stack.push_back(j - 1);
      if (x < w - 1) stack.push_back(j + 1);
      if (y > 0) stack.push_back(j - w);
      if (y < h - 1) stack.push_back(j + w);
    }
    if (area >= minArea) ++count;
  }
  return count;
}

// The matte, evaluated on demand from guided-filter coefficients solved on a
// coarse grid (no full-resolution alpha plane is ever stored).
struct Matte {
  Plane a, b;
  float inv = 1.0f;  // grid cells per full-res pixel
  float at(int32_t x, int32_t y, float luma) const {
    const float fx = (static_cast<float>(x) + 0.5f) * inv - 0.5f, fy = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
    return smoothstep(0.08f, 0.92f, std::clamp(a.sample(fx, fy) * luma + b.sample(fx, fy), 0.0f, 1.0f));
  }
};

// Area-averaged luma/chroma of a rectangle of the photo on a grid of `f`.
struct GridYcc {
  Plane y, cb, cr;
};
Result<GridYcc> gridYcc(const ImageBuffer& image, const Rect& r, int32_t f, bool chroma, const CancellationToken* token) {
  const int32_t w = (r.w() + f - 1) / f, h = (r.h() + f - 1) / f;
  GridYcc g;
  auto y = Plane::allocate(w, h);
  if (!y) return failure(y.error());
  g.y = std::move(y).value();
  if (chroma) {
    auto cb = Plane::allocate(w, h), cr = Plane::allocate(w, h);
    if (!cb || !cr) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory"));
    g.cb = std::move(cb).value();
    g.cr = std::move(cr).value();
  }
  const Rgb o = offsets(image);
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t gy = begin; gy < end; ++gy)
      for (int32_t gx = 0; gx < w; ++gx) {
        float sy = 0, scb = 0, scr = 0;
        int32_t n = 0;
        for (int32_t yy = r.y0 + gy * f; yy < std::min(r.y1, r.y0 + (gy + 1) * f); ++yy) {
          const uint8_t* row = image.row(yy);
          for (int32_t xx = r.x0 + gx * f; xx < std::min(r.x1, r.x0 + (gx + 1) * f); ++xx, ++n) {
            const uint8_t* px = row + xx * 4;
            float yv, cb, cr;
            rgbToYcc(px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f, yv, cb, cr);
            sy += yv;
            scb += cb;
            scr += cr;
          }
        }
        const float inv = 1.0f / static_cast<float>(std::max(n, 1));
        g.y.row(gy)[gx] = sy * inv;
        if (chroma) {
          g.cb.row(gy)[gx] = scb * inv;
          g.cr.row(gy)[gx] = scr * inv;
        }
      }
  });
  if (!done) return failure(cancelledError());
  return g;
}

Result<SkinResult> retouchSkin(const ImageBuffer& image, const Matte& matte, const Plane& faces, const Plane* protect,
                               const Params& p, const CancellationToken* token) {
  ScopedTimer timer("portrait.skin");
  SkinResult res;
  const int32_t W = image.width(), H = image.height();
  const float fx = static_cast<float>(faces.width()) / static_cast<float>(W);
  const float fy = static_cast<float>(faces.height()) / static_cast<float>(H);

  // Face bounding box (in image pixels), expanded to cover neck and shoulders.
  int32_t bx0 = faces.width(), by0 = faces.height(), bx1 = -1, by1 = -1;
  double faceCells = 0.0;
  for (int32_t y = 0; y < faces.height(); ++y)
    for (int32_t x = 0; x < faces.width(); ++x)
      if (faces.row(y)[x] > 0.5f) {
        bx0 = std::min(bx0, x);
        by0 = std::min(by0, y);
        bx1 = std::max(bx1, x);
        by1 = std::max(by1, y);
        faceCells += 1.0;
      }
  if (bx1 < 0) return res;
  const float fw = static_cast<float>(bx1 - bx0 + 1) / fx, fh = static_cast<float>(by1 - by0 + 1) / fy;
  const float faceScaleFull = static_cast<float>(std::sqrt(faceCells / (static_cast<double>(fx) * fy)));
  Rect r;
  r.x0 = std::clamp(static_cast<int32_t>(static_cast<float>(bx0) / fx - 0.8f * fw), 0, W);
  r.x1 = std::clamp(static_cast<int32_t>(static_cast<float>(bx1 + 1) / fx + 0.8f * fw), 0, W);
  r.y0 = std::clamp(static_cast<int32_t>(static_cast<float>(by0) / fy - 0.3f * fh), 0, H);
  r.y1 = std::clamp(static_cast<int32_t>(static_cast<float>(by1 + 1) / fy + 1.6f * fh), 0, H);
  if (r.w() < 16 || r.h() < 16 || faceScaleFull < 12.0f) return res;

  // Work where the face is ~180 px across: big faces cost no more memory.
  const int32_t scale = std::max<int32_t>(1, static_cast<int32_t>(std::lround(faceScaleFull / 180.0f)));
  auto grid = gridYcc(image, r, scale, true, token);
  if (!grid) return failure(grid.error());
  Plane& Yc = grid->y;
  Plane& Cb = grid->cb;
  Plane& Cr = grid->cr;
  const int32_t cw = Yc.width(), ch = Yc.height();
  auto F = Plane::allocate(cw, ch), Pr = Plane::allocate(cw, ch), S = Plane::allocate(cw, ch);
  if (!F || !Pr || !S) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to retouch"));
  std::vector<float> sampCb, sampCr;
  for (int32_t y = 0; y < ch; ++y)
    for (int32_t x = 0; x < cw; ++x) {
      // Centre of this grid cell in full-res pixel coordinates.
      const float px = static_cast<float>(r.x0) + (static_cast<float>(x) + 0.5f) * static_cast<float>(scale);
      const float py = static_cast<float>(r.y0) + (static_cast<float>(y) + 0.5f) * static_cast<float>(scale);
      const float fv = faces.sample(px * fx - 0.5f, py * fy - 0.5f);
      float pv = 0.0f;
      if (protect != nullptr) {
        pv = protect->sample(px * static_cast<float>(protect->width()) / static_cast<float>(W) - 0.5f,
                             py * static_cast<float>(protect->height()) / static_cast<float>(H) - 0.5f);
      }
      F->row(y)[x] = fv;
      Pr->row(y)[x] = pv;
      const float yy = Yc.row(y)[x];
      if (fv > 0.6f && pv < 0.3f && yy > 0.06f && yy < 0.98f) {
        sampCb.push_back(Cb.row(y)[x]);
        sampCr.push_back(Cr.row(y)[x]);
      }
    }
  if (sampCb.size() < 100) return res;

  // This person's own skin colour (robust statistics over the face).
  const float mcb = median(sampCb), mcr = median(sampCr);
  for (auto& v : sampCb) v = std::fabs(v - mcb);
  for (auto& v : sampCr) v = std::fabs(v - mcr);
  const float scb = std::max(0.012f, 1.4826f * median(sampCb));
  const float scr = std::max(0.012f, 1.4826f * median(sampCr));

  for (int32_t y = 0; y < ch; ++y)
    for (int32_t x = 0; x < cw; ++x) {
      const float dcb = (Cb.row(y)[x] - mcb) / scb, dcr = (Cr.row(y)[x] - mcr) / scr;
      const float d2 = dcb * dcb + dcr * dcr;
      const float like = std::exp(-0.5f * d2 / (2.5f * 2.5f));
      const float wide = std::exp(-0.5f * d2 / (4.0f * 4.0f));
      const int32_t ix = std::min(W - 1, r.x0 + x * scale + scale / 2), iy = std::min(H - 1, r.y0 + y * scale + scale / 2);
      const float a = matte.at(ix, iy, Yc.row(y)[x]);
      float s = like * smoothstep(0.15f, 0.5f, a);
      s = std::max(s, F->row(y)[x] * wide);
      S->row(y)[x] = s * (1.0f - Pr->row(y)[x]);
    }
  const float faceScale = faceScaleFull / static_cast<float>(scale);
  {
    auto Sb = Plane::allocate(cw, ch);
    if (!Sb) return failure(Sb.error());
    if (Error e = gaussianBlur(*S, *Sb, std::max(1.0f, faceScale / 60.0f), token); e.status != PP_OK) return failure(e);
    // Never let smoothing reach protected features (blur could leak into them).
    for (size_t i = 0; i < S->size(); ++i) S->data()[i] = Sb->data()[i] * (1.0f - Pr->data()[i]);
  }
  if (isCancelled(token)) return failure(cancelledError());

  const int32_t rs = std::clamp(static_cast<int32_t>(std::lround(faceScale / 40.0f)), 2, 24);
  const float healSigma = 1.5f * static_cast<float>(rs);
  // Small faces (group or full-length shots) already show no fine texture:
  // smoothing them would only erase the natural shading that shapes a face,
  // and their "spots" would really be nostrils or eye corners.
  const float strength = std::clamp(p.retouch, 0.0f, 1.0f) * smoothstep(40.0f, 140.0f, faceScaleFull);
  const bool healSpots = faceScaleFull >= 100.0f;

  // 1. Spots: small dark or red blemishes against the surrounding SKIN only
  //    (averages weighted by the skin map, so hair, background or the face
  //    outline never count as a blemish).
  auto skinAverage = [&](const Plane& v) -> Result<Plane> {
    auto num = Plane::allocate(cw, ch);
    if (!num) return failure(num.error());
    for (size_t i = 0; i < num->size(); ++i) num->data()[i] = v.data()[i] * S->data()[i];
    auto nb = blurLarge(*num, healSigma, token);
    if (!nb) return failure(nb.error());
    auto sb = blurLarge(*S, healSigma, token);
    if (!sb) return failure(sb.error());
    for (size_t i = 0; i < num->size(); ++i) {
      num->data()[i] = sb->data()[i] > 0.05f ? nb->data()[i] / sb->data()[i] : v.data()[i];
    }
    return num;
  };
  auto G = skinAverage(Yc);
  auto CrS = skinAverage(Cr);
  if (!G) return failure(G.error());
  if (!CrS) return failure(CrS.error());
  auto spot = Plane::allocate(cw, ch);
  if (!spot) return failure(spot.error());
  for (size_t i = 0; i < spot->size(); ++i) {
    const float dark = smoothstep(0.015f, 0.05f, G->data()[i] - Yc.data()[i]);
    const float red = smoothstep(0.012f, 0.035f, Cr.data()[i] - CrS->data()[i]);
    spot->data()[i] = healSpots ? std::max(dark, red) * smoothstep(0.45f, 0.75f, S->data()[i]) : 0.0f;
  }
  res.spots = strength > 0.0f && healSpots ? countSpots(*spot, std::max(6, rs * rs)) : 0;
  auto spotD = blurLarge(*spot, 0.6f * static_cast<float>(rs), token);
  if (!spotD) return failure(spotD.error());
  for (auto* v = spotD->data(); v != spotD->data() + spotD->size(); ++v) *v = std::min(1.0f, *v * 3.0f);

  // Heal by inpainting from the clean surroundings (normalised convolution).
  auto heal = [&](Plane& ch_) -> Error {
    auto num = Plane::allocate(cw, ch), wgt = Plane::allocate(cw, ch);
    if (!num || !wgt) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to retouch");
    for (size_t i = 0; i < num->size(); ++i) {
      const float w = 1.0f - spotD->data()[i];
      wgt->data()[i] = w;
      num->data()[i] = ch_.data()[i] * w;
    }
    auto nb = blurLarge(*num, healSigma, token);
    if (!nb) return nb.error();
    auto wb = blurLarge(*wgt, healSigma, token);
    if (!wb) return wb.error();
    const float m = std::min(1.0f, strength * 2.0f);
    for (size_t i = 0; i < ch_.size(); ++i) {
      const float healed = wb->data()[i] > 1e-3f ? nb->data()[i] / wb->data()[i] : ch_.data()[i];
      ch_.data()[i] += (healed - ch_.data()[i]) * spotD->data()[i] * m;
    }
    return ok();
  };
  auto Yh = clonePlane(Yc);
  auto Cbh = clonePlane(Cb);
  auto Crh = clonePlane(Cr);
  if (!Yh || !Cbh || !Crh) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to retouch"));
  if (strength > 0.0f) {
    if (Error e = heal(*Yh); e.status != PP_OK) return failure(e);
    if (Error e = heal(*Cbh); e.status != PP_OK) return failure(e);
    if (Error e = heal(*Crh); e.status != PP_OK) return failure(e);
  }

  // 2. Smooth: edge-preserving base + a share of real texture (not plastic).
  auto base = clonePlane(*Yh);
  if (!base) return failure(base.error());
  if (strength > 0.0f) {
    if (Error e = enhance::denoiseGuided(*base, rs, 0.03f * 0.03f, 0.0f, token); e.status != PP_OK) return failure(e);
  }
  const float keep = 1.0f - 0.9f * strength;

  // 3. Even blotches toward the person's own tone (never a different tone).
  auto CbS = blurLarge(*Cbh, 2.0f * static_cast<float>(rs), token);
  if (!CbS) return failure(CbS.error());
  auto CrS2 = blurLarge(*Crh, 2.0f * static_cast<float>(rs), token);
  if (!CrS2) return failure(CrS2.error());
  const float even = 0.65f * strength;
  const float lift = 0.055f * std::clamp(p.brighten, 0.0f, 1.0f);

  // Fully-retouched values; the composite blends them in by the skin weight.
  auto ret = Plane::allocate(cw, ch), dcb = Plane::allocate(cw, ch), dcr = Plane::allocate(cw, ch);
  if (!ret || !dcb || !dcr) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to retouch"));
  double skinSum = 0.0;
  for (size_t i = 0; i < ret->size(); ++i) {
    float yv = base->data()[i] + keep * (Yh->data()[i] - base->data()[i]);
    const float tcb = CbS->data()[i] + 0.3f * (mcb - CbS->data()[i]);
    const float tcr = CrS2->data()[i] + 0.3f * (mcr - CrS2->data()[i]);
    float cb = Cbh->data()[i] + even * (tcb - Cbh->data()[i]);
    float cr = Crh->data()[i] + even * (tcr - Crh->data()[i]);
    // Exposure-style lift: chroma scales with luma, so hue and saturation
    // hold (a lift without it reads as pale or ashy, on any skin tone).
    const float before = std::max(yv, 1e-3f);
    yv += lift * (1.0f - yv) * smoothstep(0.03f, 0.3f, yv);
    const float k = yv / before;
    ret->data()[i] = yv;
    dcb->data()[i] = cb * k - Cb.data()[i];
    dcr->data()[i] = cr * k - Cr.data()[i];
    skinSum += S->data()[i];
  }
  res.rect = r;
  res.scale = scale;
  res.keep = keep;
  res.skinSum = skinSum * static_cast<double>(scale) * scale;
  res.skin = std::move(S).value();
  res.base = std::move(Yc);
  res.retouched = std::move(ret).value();
  res.dcb = std::move(dcb).value();
  res.dcr = std::move(dcr).value();
  return res;
}

// Subject-free lens blur of the background on a reduced grid (linear light).
struct Bokeh {
  Plane r, g, b;
  int32_t factor = 1;
  float radius = 0.0f;
};

Result<Bokeh> lensBlur(const ImageBuffer& image, const Matte& matte, float radius, float pop,
                       const CancellationToken* token) {
  ScopedTimer timer("portrait.lensBlur");
  Bokeh out;
  out.radius = radius;
  out.factor = std::max<int32_t>(1, static_cast<int32_t>(std::lround(radius / 7.0f)));
  const int32_t d = out.factor;
  const int32_t W = image.width(), H = image.height();
  const int32_t w = (W + d - 1) / d, h = (H + d - 1) / d;
  auto Lr = Plane::allocate(w, h), Lg = Plane::allocate(w, h), Lb = Plane::allocate(w, h), Wb = Plane::allocate(w, h);
  if (!Lr || !Lg || !Lb || !Wb) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));

  float lut[256];
  for (int i = 0; i < 256; ++i) lut[i] = toLinear(static_cast<float>(i) / 255.0f);
  const Rgb o = offsets(image);
  const float bloom = 4.0f * std::clamp(radius / 60.0f, 0.3f, 1.0f);
  bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < w; ++x) {
        float r = 0, g = 0, b = 0, luma = 0;
        int32_t n = 0;
        for (int32_t yy = y * d; yy < std::min(H, (y + 1) * d); ++yy) {
          const uint8_t* row = image.row(yy);
          for (int32_t xx = x * d; xx < std::min(W, (x + 1) * d); ++xx, ++n) {
            const uint8_t* px = row + xx * 4;
            r += lut[px[o.r]];
            g += lut[px[o.g]];
            b += lut[px[o.b]];
            luma += 0.299f * px[o.r] + 0.587f * px[o.g] + 0.114f * px[o.b];
          }
        }
        const float inv = 1.0f / static_cast<float>(n);
        r *= inv;
        g *= inv;
        b *= inv;
        // One matte evaluation per cell (at its centre, from the cell's luma).
        const float a = matte.at(std::min(W - 1, x * d + d / 2), std::min(H - 1, y * d + d / 2), luma * inv / 255.0f);
        // Highlights bloom into "bokeh" discs, as they do through a real lens.
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float s = smoothstep(0.7f, 1.0f, lum);
        const float k = 1.0f + bloom * s * s;
        Lr->row(y)[x] = r * k;
        Lg->row(y)[x] = g * k;
        Lb->row(y)[x] = b * k;
        const float bgw = std::clamp(1.0f - 1.6f * a, 0.0f, 1.0f);
        Wb->row(y)[x] = bgw * bgw;
      }
  });
  if (!done) return failure(cancelledError());

  // Anti-aliased disc kernel.
  const float rl = radius / static_cast<float>(d);
  const int32_t R = static_cast<int32_t>(std::ceil(rl + 0.5f));
  struct Tap {
    int32_t dx, dy;
    float w;
  };
  std::vector<Tap> taps;
  for (int32_t dy = -R; dy <= R; ++dy)
    for (int32_t dx = -R; dx <= R; ++dx) {
      const float wgt = std::clamp(rl + 0.5f - std::sqrt(static_cast<float>(dx * dx + dy * dy)), 0.0f, 1.0f);
      if (wgt > 0.0f) taps.push_back({dx, dy, wgt});
    }
  auto Br = Plane::allocate(w, h), Bg = Plane::allocate(w, h), Bb = Plane::allocate(w, h);
  if (!Br || !Bg || !Bb) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));
  done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y)
      for (int32_t x = 0; x < w; ++x) {
        float nr = 0, ng = 0, nb = 0, den = 0, pr = 0, pg = 0, pb = 0, ks = 0;
        for (const Tap& t : taps) {
          const int32_t xx = std::clamp(x + t.dx, 0, w - 1), yy = std::clamp(y + t.dy, 0, h - 1);
          const float bw = t.w * Wb->row(yy)[xx];
          const float vr = Lr->row(yy)[xx], vg = Lg->row(yy)[xx], vb = Lb->row(yy)[xx];
          nr += bw * vr;
          ng += bw * vg;
          nb += bw * vb;
          den += bw;
          pr += t.w * vr;
          pg += t.w * vg;
          pb += t.w * vb;
          ks += t.w;
        }
        float r, g, b;
        if (den > 0.02f * ks) {
          r = nr / den;
          g = ng / den;
          b = nb / den;
        } else {  // deep inside the subject: hidden by the matte anyway
          r = pr / ks;
          g = pg / ks;
          b = pb / ks;
        }
        // Soft-clip the bloomed highlights, then separate the background a
        // touch (slightly calmer colour and light) so the subject leads.
        auto knee = [](float v) { return v <= 0.9f ? v : 0.9f + 0.1f * (1.0f - std::exp(-(v - 0.9f) / 0.1f)); };
        r = knee(r);
        g = knee(g);
        b = knee(b);
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float desat = 0.12f * pop, dim = 1.0f - 0.06f * pop;
        Br->row(y)[x] = (r + desat * (lum - r)) * dim;
        Bg->row(y)[x] = (g + desat * (lum - g)) * dim;
        Bb->row(y)[x] = (b + desat * (lum - b)) * dim;
      }
  });
  if (!done) return failure(cancelledError());
  out.r = std::move(Br).value();
  out.g = std::move(Bg).value();
  out.b = std::move(Bb).value();
  return out;
}

}  // namespace

Error apply(ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const Params& params, const CancellationToken* token, Report* report) {
  ScopedTimer timer("portrait.apply");
  if (!isColour(image)) return makeError(PP_ERROR_INVALID_ARGUMENT, "Portrait needs an RGBA image");
  if (image.width() < 16 || image.height() < 16) return makeError(PP_ERROR_INVALID_ARGUMENT, "Image too small");
  if (subject.empty() || subject.width() < 2 || subject.height() < 2) {
    return makeError(PP_ERROR_INVALID_ARGUMENT, "Subject mask is required");
  }
  Params p = params;
  p.blur = std::clamp(p.blur, 0.0f, 1.0f);
  p.retouch = std::clamp(p.retouch, 0.0f, 1.0f);
  p.brighten = std::clamp(p.brighten, 0.0f, 1.0f);
  p.pop = std::clamp(p.pop, 0.0f, 1.0f);
  const int32_t W = image.width(), H = image.height();
  const float shortSide = static_cast<float>(std::min(W, H));
  const Rect full{0, 0, W, H};

  // Sensor grain, measured on a full-resolution centre window.
  float grain = 0.0f;
  {
    const int32_t cw = std::min(W, 512), ch = std::min(H, 512);
    const Rect c{(W - cw) / 2, (H - ch) / 2, (W - cw) / 2 + cw, (H - ch) / 2 + ch};
    auto g = gridYcc(image, c, 1, false, token);
    if (!g) return g.error();
    grain = enhance::estimateNoise(g->y);
  }

  // 1. Matte aligned to the photo's own edges (fast guided filter: the
  //    coefficients live on a coarse grid; alpha is evaluated per pixel).
  Matte matte;
  {
    const auto radius = std::max<int32_t>(4, static_cast<int32_t>(shortSide / 150.0f));
    // Coefficients are smooth, so a grid of ~1024 px is plenty; the per-pixel
    // a·I + b evaluation restores full-resolution edges.
    const int32_t s = std::max<int32_t>({1, radius / 4, (std::max(W, H) + 1023) / 1024});
    auto guide = gridYcc(image, full, s, false, token);
    if (!guide) return guide.error();
    auto coarse = maskPlane(subject);
    if (!coarse) return coarse.error();
    auto pl = resizeBilinear(*coarse, guide->y.width(), guide->y.height(), token);
    if (!pl) return pl.error();
    auto a = Plane::allocate(guide->y.width(), guide->y.height());
    auto b = Plane::allocate(guide->y.width(), guide->y.height());
    if (!a || !b) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    const int32_t rl = std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<float>(radius) / static_cast<float>(s))));
    if (Error e = guidedCoefficients(guide->y, *pl, rl, 2e-4f, *a, *b, token); e.status != PP_OK) return e;
    matte.a = std::move(a).value();
    matte.b = std::move(b).value();
    matte.inv = 1.0f / static_cast<float>(s);
  }

  // 2. Skin (on a face-sized grid).
  SkinResult skin;
  if (faces != nullptr && !faces->empty() && (p.retouch > 0.0f || p.brighten > 0.0f)) {
    auto F = maskPlane(*faces);
    if (!F) return F.error();
    std::optional<Plane> Pr;
    if (protect != nullptr && !protect->empty()) {
      auto pp = maskPlane(*protect);
      if (!pp) return pp.error();
      Pr = std::move(pp).value();
    }
    auto s = retouchSkin(image, matte, *F, Pr ? &*Pr : nullptr, p, token);
    if (!s) return s.error();
    skin = std::move(s).value();
  }

  // 3. Coarse luma for subject clarity.
  const float claritySigma = std::max(2.0f, 0.01f * shortSide);
  const int32_t cf = std::max<int32_t>(1, static_cast<int32_t>(claritySigma / 3.0f));
  Plane clarityBase;
  if (p.pop > 0.0f) {
    auto g = gridYcc(image, full, cf, false, token);
    if (!g) return g.error();
    auto blurred = Plane::allocate(g->y.width(), g->y.height());
    if (!blurred) return blurred.error();
    if (Error e = gaussianBlur(g->y, *blurred, claritySigma / static_cast<float>(cf), token); e.status != PP_OK) return e;
    clarityBase = std::move(blurred).value();
  }

  // 4. Background lens blur.
  const float blurRadius = p.blur * 0.04f * shortSide;
  std::optional<Bokeh> bokeh;
  if (blurRadius >= 1.5f) {
    auto b = lensBlur(image, matte, blurRadius, p.pop, token);
    if (!b) return b.error();
    bokeh = std::move(b).value();
  }

  // 5. One streaming pass: retouch (frequency separation), clarity, lift,
  //    vibrance, and the linear-light composite along the matte.
  const Rgb o = offsets(image);
  // Matched (slightly lighter) grain keeps blurred areas from looking
  // plastic next to the real subject; capped so results stay clean.
  const float grainAmt = bokeh ? std::min(0.6f * grain, 0.008f) : 0.0f;
  const float invCf = 1.0f / static_cast<float>(cf);
  std::vector<double> rowCoverage(static_cast<size_t>(H), 0.0);
  const bool done = parallelFor(H, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y) {
      uint8_t* row = image.row(y);
      double cov = 0.0;
      for (int32_t x = 0; x < W; ++x) {
        uint8_t* px = row + x * 4;
        float y0, cb, cr;
        rgbToYcc(px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f, y0, cb, cr);
        const float a = matte.at(x, y, y0);
        cov += a;
        float yv = y0, sv = 0.0f;
        if (skin.active() && skin.rect.contains(x, y)) {
          const float inv = 1.0f / static_cast<float>(skin.scale);
          const float gx = (static_cast<float>(x - skin.rect.x0) + 0.5f) * inv - 0.5f;
          const float gy = (static_cast<float>(y - skin.rect.y0) + 0.5f) * inv - 0.5f;
          sv = skin.skin.sample(gx, gy);
          if (sv > 0.0f) {
            const float base = skin.scale == 1 ? y0 : skin.base.sample(gx, gy);
            yv = y0 + sv * ((skin.retouched.sample(gx, gy) - base) + (skin.keep - 1.0f) * (y0 - base));
            cb += sv * skin.dcb.sample(gx, gy);
            cr += sv * skin.dcr.sample(gx, gy);
          }
        }
        if (a > 0.0f) {
          if (p.pop > 0.0f) {
            const float cbase = clarityBase.sample((static_cast<float>(x) + 0.5f) * invCf - 0.5f,
                                                   (static_cast<float>(y) + 0.5f) * invCf - 0.5f);
            yv += p.pop * 0.4f * (y0 - cbase) * a * (1.0f - 0.85f * sv);
            const float sat = std::sqrt(cb * cb + cr * cr);
            const float k = 1.0f + p.pop * 0.18f * a * (1.0f - sv) * (1.0f - std::min(1.0f, sat / 0.25f));
            cb *= k;
            cr *= k;
          }
          const float before = std::max(yv, 1e-3f);
          yv += p.brighten * 0.035f * a * (1.0f - yv) * smoothstep(0.02f, 0.25f, yv);
          const float k = yv / before;  // keep saturation as light is added
          cb *= k;
          cr *= k;
        }
        float r, g, b;
        yccToRgb(yv, cb, cr, r, g, b);
        if (bokeh) {
          const float d = static_cast<float>(bokeh->factor);
          const float bx = (static_cast<float>(x) + 0.5f) / d - 0.5f, by = (static_cast<float>(y) + 0.5f) / d - 0.5f;
          const float n = grainAmt * (1.0f - a) * hashNoise(x, y, 0x5EEDu);
          r = toGamma(a * toLinear(r) + (1.0f - a) * bokeh->r.sample(bx, by)) + n;
          g = toGamma(a * toLinear(g) + (1.0f - a) * bokeh->g.sample(bx, by)) + n;
          b = toGamma(a * toLinear(b) + (1.0f - a) * bokeh->b.sample(bx, by)) + n;
        }
        px[o.r] = toByte(r);
        px[o.g] = toByte(g);
        px[o.b] = toByte(b);
      }
      rowCoverage[static_cast<size_t>(y)] = cov;
    }
  });
  if (!done) return cancelledError();

  if (report != nullptr) {
    double coverage = 0.0;
    for (double c : rowCoverage) coverage += c;
    const double n = static_cast<double>(W) * H;
    report->subjectCoverage = static_cast<float>(coverage / n);
    report->skinCoverage = static_cast<float>(skin.skinSum / n);
    report->spotsRemoved = skin.spots;
    report->blurRadius = bokeh ? blurRadius : 0.0f;
  }
  return ok();
}

}  // namespace photoplus::portrait
