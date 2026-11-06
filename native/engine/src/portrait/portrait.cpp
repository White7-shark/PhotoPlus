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
#include <array>
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
  float skinLuma = 0.5f;  // the person's median skin luma
  int32_t spots = 0;
  double skinSum = 0.0;   // in full-res pixels

  bool active() const { return !skin.empty(); }
};

// Keeps blemish-sized clusters only: stray pixels are not reported, and
// anything larger than a blemish (a nostril, a crease, the shadow under the
// jaw) is a feature of the face, so it is cleared from the map, not healed.
// Returns the number of blemishes.
int32_t keepBlemishes(Plane& spot, int32_t minArea, int32_t maxArea) {
  const int32_t w = spot.width(), h = spot.height();
  std::vector<uint8_t> seen(spot.size(), 0);
  std::vector<int32_t> stack, members;
  int32_t count = 0;
  for (int32_t i = 0; i < w * h; ++i) {
    if (seen[static_cast<size_t>(i)] || spot.data()[i] < 0.5f) continue;
    // Connected through the soft fringe; sized by its solid core.
    int32_t core = 0;
    members.clear();
    stack.push_back(i);
    while (!stack.empty()) {
      const int32_t j = stack.back();
      stack.pop_back();
      if (seen[static_cast<size_t>(j)] || spot.data()[j] < 0.1f) continue;
      seen[static_cast<size_t>(j)] = 1;
      members.push_back(j);
      if (spot.data()[j] >= 0.5f) ++core;
      const int32_t x = j % w, y = j / w;
      if (x > 0) stack.push_back(j - 1);
      if (x < w - 1) stack.push_back(j + 1);
      if (y > 0) stack.push_back(j - w);
      if (y < h - 1) stack.push_back(j + w);
    }
    if (core > maxArea) {
      for (const int32_t j : members) spot.data()[j] = 0.0f;
    } else if (core >= minArea) {
      ++count;
    }
  }
  return count;
}

// The matte, evaluated on demand from guided-filter coefficients solved on a
// coarse grid (no full-resolution alpha plane is ever stored).
struct Matte {
  Plane a, b;
  // Colour matte (edge refinement): coefficients for gamma R, G, B.
  bool color = false;
  std::array<Plane, 3> ac;
  float inv = 1.0f;  // grid cells per full-res pixel
  // Raw (unsharpened) alpha at a pixel of gamma colour (r, g, b).
  float raw(int32_t x, int32_t y, float r, float g, float bl) const {
    const float fx = (static_cast<float>(x) + 0.5f) * inv - 0.5f, fy = (static_cast<float>(y) + 0.5f) * inv - 0.5f;
    const float v = color ? ac[0].sample(fx, fy) * r + ac[1].sample(fx, fy) * g + ac[2].sample(fx, fy) * bl + b.sample(fx, fy)
                          : a.sample(fx, fy) * (0.299f * r + 0.587f * g + 0.114f * bl) + b.sample(fx, fy);
    return std::clamp(v, 0.0f, 1.0f);
  }
  float at(int32_t x, int32_t y, float r, float g, float bl) const { return smoothstep(0.08f, 0.92f, raw(x, y, r, g, bl)); }
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
  if (r.w() < 16 || r.h() < 16 || faceScaleFull < 20.0f) return res;

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
  std::vector<float> sampCb, sampCr, sampY;
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
        sampY.push_back(yy);
      }
    }
  if (sampCb.size() < 100) return res;

  // This person's own skin colour (robust statistics over the face).
  const float mcb = median(sampCb), mcr = median(sampCr);
  // The person's own skin brightness: every retouch threshold is relative to
  // it, so deep and light skin are treated alike (absolute thresholds would
  // flatten the shading of deep skin and miss its blemishes).
  const float mY = std::max(0.08f, median(sampY));
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
      float cr_, cg_, cb_;
      yccToRgb(Yc.row(y)[x], Cb.row(y)[x], Cr.row(y)[x], cr_, cg_, cb_);
      const float a = matte.at(ix, iy, cr_, cg_, cb_);
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
  const float sizeK = smoothstep(20.0f, 90.0f, faceScaleFull);
  const float sSmooth = std::clamp(p.smoothAmount(), 0.0f, 1.0f) * sizeK;
  const float sHeal = std::clamp(p.blemishAmount(), 0.0f, 1.0f) * sizeK;
  const float sEven = std::clamp(p.evenAmount(), 0.0f, 1.0f) * sizeK;
  const bool healSpots = faceScaleFull >= 28.0f;

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
    const float g = std::max(G->data()[i], 0.25f * mY);
    const float dark = smoothstep(0.035f, 0.1f, (G->data()[i] - Yc.data()[i]) / g);
    const float red = smoothstep(0.025f, 0.07f, (Cr.data()[i] - CrS->data()[i]) / std::max(g, 0.12f));
    spot->data()[i] = healSpots ? std::max(dark, red) * smoothstep(0.45f, 0.75f, S->data()[i]) : 0.0f;
  }
  {
    const float rMax = 0.03f * faceScale;
    const int32_t maxArea = std::max(12, static_cast<int32_t>(3.14159f * rMax * rMax));
    const int32_t n = keepBlemishes(*spot, std::max(6, rs * rs), maxArea);
    res.spots = sHeal > 0.0f && healSpots ? n : 0;
  }
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
    const float m = std::min(1.0f, sHeal * 2.0f);
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
  if (sHeal > 0.0f) {
    if (Error e = heal(*Yh); e.status != PP_OK) return failure(e);
    if (Error e = heal(*Cbh); e.status != PP_OK) return failure(e);
    if (Error e = heal(*Crh); e.status != PP_OK) return failure(e);
  }

  // 2. Smooth: edge-preserving base + a share of real texture (not plastic).
  auto base = clonePlane(*Yh);
  if (!base) return failure(base.error());
  if (sSmooth > 0.0f) {
    if (Error e = enhance::denoiseGuided(*base, rs, std::max(0.07f * mY, 0.01f) * std::max(0.07f * mY, 0.01f), 0.0f, token); e.status != PP_OK) return failure(e);
  }
  const float keep = 1.0f - 0.9f * sSmooth;

  // 3. Even blotches toward the person's own tone (never a different tone).
  auto CbS = blurLarge(*Cbh, 2.0f * static_cast<float>(rs), token);
  if (!CbS) return failure(CbS.error());
  auto CrS2 = blurLarge(*Crh, 2.0f * static_cast<float>(rs), token);
  if (!CrS2) return failure(CrS2.error());
  auto YS = blurLarge(*Yh, 2.0f * static_cast<float>(rs), token);
  if (!YS) return failure(YS.error());
  const float even = 0.65f * sEven;
  const float lift = 0.07f * std::clamp(p.brighten, 0.0f, 1.0f);

  // Fully-retouched values; the composite blends them in by the skin weight.
  auto ret = Plane::allocate(cw, ch), dcb = Plane::allocate(cw, ch), dcr = Plane::allocate(cw, ch);
  if (!ret || !dcb || !dcr) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory to retouch"));
  double skinSum = 0.0;
  for (size_t i = 0; i < ret->size(); ++i) {
    float yv = base->data()[i] + keep * (Yh->data()[i] - base->data()[i]);
    // Target colour at this cell's own brightness: chroma scales with luma,
    // so a darker feature keeps proportionally less colour (never a tint).
    const float rel = std::clamp((Yh->data()[i] + 0.02f) / (YS->data()[i] + 0.02f), 0.0f, 1.5f);
    const float tcb = rel * (CbS->data()[i] + 0.3f * (mcb - CbS->data()[i]));
    const float tcr = rel * (CrS2->data()[i] + 0.3f * (mcr - CrS2->data()[i]));
    float cb = Cbh->data()[i] + even * (tcb - Cbh->data()[i]);
    float cr = Crh->data()[i] + even * (tcr - Crh->data()[i]);
    // Exposure-style lift: chroma scales with luma, so hue and saturation
    // hold (a lift without it reads as pale or ashy, on any skin tone).
    yv = std::max(yv, 0.0f);
    const float before = yv;
    yv = std::min(1.0f, yv * (1.0f + lift * (1.0f - yv) * smoothstep(0.03f, 0.3f, yv)));
    const float k = std::clamp((yv + 0.02f) / (before + 0.02f), 0.0f, 1.25f);
    ret->data()[i] = yv;
    dcb->data()[i] = cb * k - Cb.data()[i];
    dcr->data()[i] = cr * k - Cr.data()[i];
    skinSum += S->data()[i];
  }
  res.rect = r;
  res.scale = scale;
  res.keep = keep;
  res.skinLuma = mY;
  res.skinSum = skinSum * static_cast<double>(scale) * scale;
  res.skin = std::move(S).value();
  res.base = std::move(Yc);
  res.retouched = std::move(ret).value();
  res.dcb = std::move(dcb).value();
  res.dcr = std::move(dcr).value();
  return res;
}

// Depth-aware lens blur on a reduced grid, in linear light.
//
// Like a real lens (and flagship phones' portrait modes), each area's blur
// grows with its depth distance from the subject's focus plane: the ground
// at the subject's feet and anything beside them stays sharp, the ground
// further back softens progressively, the far background melts, and things
// nearer than the subject blur too. The image is split into depth bands;
// each band is blurred by its own amount and they are composited back to
// front, so a blurred background never bleeds over nearer things.
struct Bokeh {
  Plane farR, farG, farB;                // background behind the focus plane (hole-filled)
  Plane nearR, nearG, nearB, nearA;      // in front of the focus plane (premultiplied)
  Plane focus;                           // weight of the sharp, in-focus band
  int32_t factor = 1;
  float radius = 0.0f;
};

namespace {

struct Tap {
  int32_t dx, dy;
  float w;
};

std::vector<Tap> discTaps(float radius) {
  std::vector<Tap> taps;
  const auto R = static_cast<int32_t>(std::ceil(radius + 0.5f));
  for (int32_t dy = -R; dy <= R; ++dy)
    for (int32_t dx = -R; dx <= R; ++dx) {
      const float wgt = std::clamp(radius + 0.5f - std::sqrt(static_cast<float>(dx * dx + dy * dy)), 0.0f, 1.0f);
      if (wgt > 0.0f) taps.push_back({dx, dy, wgt});
    }
  return taps;
}

// Disc blur of premultiplied colour + weight planes (same kernel for all).
Error discBlur(const std::array<const Plane*, 4>& src, std::array<Plane, 4>& dst, float radius,
               const CancellationToken* token) {
  const int32_t w = src[0]->width(), h = src[0]->height();
  for (auto& p : dst) {
    auto a = Plane::allocate(w, h);
    if (!a) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur");
    p = std::move(a).value();
  }
  const auto taps = discTaps(std::max(radius, 0.5f));
  float ks = 0.0f;
  for (const Tap& t : taps) ks += t.w;
  const float inv = 1.0f / ks;
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end && !isCancelled(token); ++y)
      for (int32_t x = 0; x < w; ++x) {
        float acc[4] = {0, 0, 0, 0};
        for (const Tap& t : taps) {
          const int32_t xx = std::clamp(x + t.dx, 0, w - 1), yy = std::clamp(y + t.dy, 0, h - 1);
          for (int c = 0; c < 4; ++c) acc[c] += t.w * src[static_cast<size_t>(c)]->row(yy)[xx];
        }
        for (int c = 0; c < 4; ++c) dst[static_cast<size_t>(c)].row(y)[x] = acc[c] * inv;
      }
  });
  return done ? ok() : cancelledError();
}

float weightedMedian(std::vector<std::pair<float, float>>& v) {
  if (v.empty()) return 0.5f;
  std::sort(v.begin(), v.end());
  double total = 0.0;
  for (const auto& e : v) total += e.second;
  double acc = 0.0;
  for (const auto& e : v) {
    acc += e.second;
    if (acc >= total * 0.5) return e.first;
  }
  return v.back().first;
}

}  // namespace

// Exposed for tests through Report: the focus plane and how depth was known.
struct DepthInfo {
  float focus = 0.0f;
  bool estimated = false;
};

Result<Bokeh> depthBokeh(const ImageBuffer& image, const Matte& matte, const Plane* depth, float radius,
                         DepthInfo* info, const CancellationToken* token) {
  ScopedTimer timer("portrait.depthBokeh");
  Bokeh out;
  out.radius = radius;
  out.factor = std::max<int32_t>(1, static_cast<int32_t>(std::lround(radius / 7.0f)));
  const int32_t d = out.factor;
  const int32_t W = image.width(), H = image.height();
  const int32_t w = (W + d - 1) / d, h = (H + d - 1) / d;
  auto alloc = [&]() { return Plane::allocate(w, h); };
  auto Lr = alloc(), Lg = alloc(), Lb = alloc(), A = alloc(), D = alloc(), Yg = alloc();
  if (!Lr || !Lg || !Lb || !A || !D || !Yg) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));

  float lut[256];
  for (int i = 0; i < 256; ++i) lut[i] = toLinear(static_cast<float>(i) / 255.0f);
  const Rgb o = offsets(image);
  const float bloom = 4.0f * std::clamp(radius / 60.0f, 0.3f, 1.0f);
  bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < w; ++x) {
        float r = 0, g = 0, b = 0, luma = 0, gr = 0, gg = 0, gb = 0;
        int32_t n = 0;
        for (int32_t yy = y * d; yy < std::min(H, (y + 1) * d); ++yy) {
          const uint8_t* row = image.row(yy);
          for (int32_t xx = x * d; xx < std::min(W, (x + 1) * d); ++xx, ++n) {
            const uint8_t* px = row + xx * 4;
            r += lut[px[o.r]];
            g += lut[px[o.g]];
            b += lut[px[o.b]];
            luma += 0.299f * px[o.r] + 0.587f * px[o.g] + 0.114f * px[o.b];
            gr += px[o.r];
            gg += px[o.g];
            gb += px[o.b];
          }
        }
        const float inv = 1.0f / static_cast<float>(n);
        r *= inv;
        g *= inv;
        b *= inv;
        Yg->row(y)[x] = luma * inv / 255.0f;
        const float g255 = inv / 255.0f;
        A->row(y)[x] = matte.at(std::min(W - 1, x * d + d / 2), std::min(H - 1, y * d + d / 2), gr * g255, gg * g255, gb * g255);
        // Highlights bloom into "bokeh" discs, as they do through a real lens.
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float s = smoothstep(0.7f, 1.0f, lum);
        const float k = 1.0f + bloom * s * s;
        Lr->row(y)[x] = r * k;
        Lg->row(y)[x] = g * k;
        Lb->row(y)[x] = b * k;
      }
  });
  if (!done) return failure(cancelledError());

  // Disparity per cell (near = 1). From the depth map when there is one,
  // otherwise a ground-plane estimate anchored at the subject's feet.
  float focus = 0.6f;
  // The in-focus range spans the subject's own depth: a standing person's feet
  // touch the ground, so the ground where they stand stays sharp too.
  float focusNear = focus, focusFar = focus;
  if (depth != nullptr) {
    const float sx = static_cast<float>(depth->width()) / static_cast<float>(w);
    const float sy = static_cast<float>(depth->height()) / static_cast<float>(h);
    auto raw = alloc();
    if (!raw) return failure(raw.error());
    for (int32_t y = 0; y < h; ++y)
      for (int32_t x = 0; x < w; ++x) {
        raw->row(y)[x] = depth->sample((static_cast<float>(x) + 0.5f) * sx - 0.5f, (static_cast<float>(y) + 0.5f) * sy - 0.5f);
      }
    // Depth estimates are slightly noisy: smooth them along the photo's own
    // edges so blur changes gradually across a surface yet stays crisp at
    // object outlines.
    auto ga = alloc(), gb = alloc();
    if (!ga || !gb) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));
    if (Error e = guidedCoefficients(*Yg, *raw, 2, 4e-3f, *ga, *gb, token); e.status != PP_OK) return failure(e);
    std::vector<std::pair<float, float>> subjectDepths;
    for (size_t c = 0; c < D->size(); ++c) {
      D->data()[c] = std::clamp(ga->data()[c] * Yg->data()[c] + gb->data()[c], 0.0f, 1.0f);
      const float a = A->data()[c];
      if (a > 0.8f) subjectDepths.emplace_back(D->data()[c], a);
    }
    if (subjectDepths.empty()) {
      focus = focusNear = focusFar = D->row(h / 2)[w / 2];
    } else {
      // Robust subject depth range (a stray hand or hair doesn't stretch it).
      focus = weightedMedian(subjectDepths);
      std::vector<float> v;
      v.reserve(subjectDepths.size());
      for (const auto& e : subjectDepths) v.push_back(e.first);
      std::sort(v.begin(), v.end());
      // A subject is shallow front to back, so the far edge hugs its depth;
      // the near edge reaches its closest part (feet, an outstretched hand).
      focusFar = std::max(v[v.size() / 4], focus - 0.03f);
      focusNear = std::max(v[std::min(v.size() - 1, v.size() * 95 / 100)], focus);
    }
  } else {
    int32_t top = -1, bottom = -1;
    for (int32_t y = 0; y < h; ++y) {
      int32_t cells = 0;
      for (int32_t x = 0; x < w; ++x) cells += A->row(y)[x] > 0.5f ? 1 : 0;
      if (cells >= std::max(1, w / 100)) {
        if (top < 0) top = y;
        bottom = y;
      }
    }
    const bool standing = bottom >= 0 && bottom < h - 2;
    for (int32_t y = 0; y < h; ++y) {
      float v;
      if (standing) {
        // Ground in front of the feet comes closer; behind, it recedes.
        v = y >= bottom ? focus + 0.6f * static_cast<float>(y - bottom) / static_cast<float>(h - bottom)
                        : focus - 0.9f * std::min(1.0f, static_cast<float>(bottom - y) / (0.55f * static_cast<float>(h)));
      } else {
        // Head-and-shoulders: everything around the subject is well behind it,
        // a touch less so toward the bottom of the frame.
        v = focus - 0.5f - 0.35f * (1.0f - static_cast<float>(y) / static_cast<float>(h));
      }
      for (int32_t x = 0; x < w; ++x) D->row(y)[x] = v;
    }
  }
  if (info != nullptr) {
    info->focus = focus;
    info->estimated = depth == nullptr;
  }

  // Blur radius (grid pixels) from the depth distance to the focus plane.
  const float Rg = radius / static_cast<float>(d);
  // Like opening up a lens: stronger blur also means a thinner slice of the
  // scene in focus, so things just behind the subject separate more.
  const float strength = std::clamp(radius / (0.04f * static_cast<float>(std::min(W, H))), 0.0f, 1.0f);
  const float kDead = 0.05f - 0.03f * strength;  // a thin slab around the subject stays sharp
  const float kSpan = 0.45f - 0.25f * strength;  // depth distance at which blur is full
  constexpr float kNear = 0.75f;   // nearer things blur a little less
  constexpr int kFar = 4, kNearBands = 2;
  auto Rc = alloc();
  auto farSide = alloc();
  if (!Rc || !farSide) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const float a = A->row(y)[x];
      const float dv = D->row(y)[x];
      const float off = dv < focusFar ? dv - focusFar : dv > focusNear ? dv - focusNear : 0.0f;
      const float delta = off * (1.0f - a);  // the subject itself is in focus
      const float t = std::clamp((std::fabs(delta) - kDead) / kSpan, 0.0f, 1.0f);
      const bool far = delta < 0.0f;
      Rc->row(y)[x] = Rg * t * (far ? 1.0f : kNear);
      farSide->row(y)[x] = far ? 1.0f : 0.0f;
    }

  // Tent weights between band radii (bands overlap, so transitions are smooth).
  auto bandWeight = [&](float r, float levelStep, int k) {
    const float level = static_cast<float>(k) * levelStep;
    return std::max(0.0f, 1.0f - std::fabs(r - level) / levelStep);
  };
  const float farStep = Rg / kFar, nearStep = kNear * Rg / kNearBands;

  auto focusW = alloc();
  if (!focusW) return failure(focusW.error());
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < w; ++x) {
      const float r = Rc->row(y)[x];
      const bool far = farSide->row(y)[x] > 0.5f;
      focusW->row(y)[x] = std::max(A->row(y)[x], bandWeight(r, far ? farStep : nearStep, 0));
    }

  // Composite one side's bands back to front: C = blur(P_k) + (1 − blur(A_k))·C.
  auto compose = [&](bool far, int bands, float step, std::array<Plane, 4>& acc) -> Error {
    for (auto& p : acc) {
      auto a = alloc();
      if (!a) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur");
      std::fill(a->data(), a->data() + a->size(), 0.0f);
      p = std::move(a).value();
    }
    // Farthest band first: on the far side that is the widest blur; on the
    // near side, the band closest to the focus plane.
    for (int i = 0; i < bands; ++i) {
      const int k = far ? bands - i : 1 + i;
      auto pr = alloc(), pg = alloc(), pb = alloc(), pa = alloc();
      if (!pr || !pg || !pb || !pa) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur");
      bool any = false;
      for (size_t c = 0; c < pr->size(); ++c) {
        const bool side = (farSide->data()[c] > 0.5f) == far;
        const float wk = side ? bandWeight(Rc->data()[c], step, k) * (1.0f - A->data()[c]) : 0.0f;
        any = any || wk > 0.0f;
        pr->data()[c] = wk * Lr->data()[c];
        pg->data()[c] = wk * Lg->data()[c];
        pb->data()[c] = wk * Lb->data()[c];
        pa->data()[c] = wk;
      }
      if (!any) continue;
      std::array<Plane, 4> blurred;
      if (Error e = discBlur({&*pr, &*pg, &*pb, &*pa}, blurred, static_cast<float>(k) * step, token); e.status != PP_OK) return e;
      for (size_t c = 0; c < acc[0].size(); ++c) {
        const float keep = 1.0f - std::min(1.0f, blurred[3].data()[c]);
        for (int ch = 0; ch < 4; ++ch) acc[static_cast<size_t>(ch)].data()[c] = blurred[static_cast<size_t>(ch)].data()[c] + keep * acc[static_cast<size_t>(ch)].data()[c];
      }
    }
    return ok();
  };

  std::array<Plane, 4> farAcc, nearAcc;
  if (Error e = compose(true, kFar, farStep, farAcc); e.status != PP_OK) return failure(e);
  if (Error e = compose(false, kNearBands, nearStep, nearAcc); e.status != PP_OK) return failure(e);

  // Behind the subject (and the sharp slab) the far layer has holes: fill
  // them with a wide, subject-free blur so edges never pick up subject colour.
  std::array<Plane, 4> fill;
  {
    auto fr = alloc(), fg = alloc(), fb = alloc(), fa = alloc();
    if (!fr || !fg || !fb || !fa) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));
    for (size_t c = 0; c < fr->size(); ++c) {
      const float bw = std::clamp(1.0f - 1.6f * A->data()[c], 0.0f, 1.0f);
      const float wgt = bw * bw;
      fr->data()[c] = wgt * Lr->data()[c];
      fg->data()[c] = wgt * Lg->data()[c];
      fb->data()[c] = wgt * Lb->data()[c];
      fa->data()[c] = wgt;
    }
    if (Error e = discBlur({&*fr, &*fg, &*fb, &*fa}, fill, std::max(Rg * 0.5f, 1.0f), token); e.status != PP_OK) return failure(e);
  }
  auto knee = [](float v) { return v <= 0.9f ? v : 0.9f + 0.1f * (1.0f - std::exp(-(v - 0.9f) / 0.1f)); };
  auto fR = alloc(), fG = alloc(), fB = alloc();
  if (!fR || !fG || !fB) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the blur"));
  for (size_t c = 0; c < fR->size(); ++c) {
    const float ca = farAcc[3].data()[c];
    const float fa = fill[3].data()[c];
    const float gap = std::max(0.0f, 0.05f - ca);
    auto mix = [&](float acc, float fl) {
      const float fillColour = fa > 1e-4f ? fl / fa : acc / std::max(ca, 1e-4f);
      return knee((acc + gap * fillColour) / std::max(ca, 0.05f));
    };
    fR->data()[c] = mix(farAcc[0].data()[c], fill[0].data()[c]);
    fG->data()[c] = mix(farAcc[1].data()[c], fill[1].data()[c]);
    fB->data()[c] = mix(farAcc[2].data()[c], fill[2].data()[c]);
    for (int ch = 0; ch < 3; ++ch) nearAcc[static_cast<size_t>(ch)].data()[c] = knee(nearAcc[static_cast<size_t>(ch)].data()[c]);
  }
  out.farR = std::move(fR).value();
  out.farG = std::move(fG).value();
  out.farB = std::move(fB).value();
  out.nearR = std::move(nearAcc[0]);
  out.nearG = std::move(nearAcc[1]);
  out.nearB = std::move(nearAcc[2]);
  out.nearA = std::move(nearAcc[3]);
  out.focus = std::move(focusW).value();
  return out;
}

// The background's own colour near the subject, in linear light: a blur of
// background pixels only (normalised convolution weighted by 1 - alpha), so
// the subject's colour never enters it. Used to decontaminate edge pixels.
struct BackgroundEstimate {
  std::array<Plane, 3> c;
  int32_t factor = 1;
};

Result<BackgroundEstimate> backgroundEstimate(const ImageBuffer& image, const Matte& matte, float shortSide,
                                              const CancellationToken* token) {
  BackgroundEstimate out;
  const int32_t d = std::max<int32_t>(1, static_cast<int32_t>(std::lround(shortSide / 240.0f)));
  out.factor = d;
  const int32_t W = image.width(), H = image.height();
  const int32_t w = (W + d - 1) / d, h = (H + d - 1) / d;
  std::array<Plane, 3> num;
  auto wgt = Plane::allocate(w, h);
  if (!wgt) return failure(wgt.error());
  for (auto& q : num) {
    auto pl = Plane::allocate(w, h);
    if (!pl) return failure(pl.error());
    q = std::move(pl).value();
  }
  float lut[256];
  for (int i = 0; i < 256; ++i) lut[i] = toLinear(static_cast<float>(i) / 255.0f);
  const Rgb o = offsets(image);
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < w; ++x) {
        float r = 0, g = 0, b = 0, wsum = 0;
        for (int32_t yy = y * d; yy < std::min(H, (y + 1) * d); ++yy) {
          const uint8_t* row = image.row(yy);
          for (int32_t xx = x * d; xx < std::min(W, (x + 1) * d); ++xx) {
            const uint8_t* px = row + xx * 4;
            const float a = matte.raw(xx, yy, px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f);
            const float wv = (1.0f - a) * (1.0f - a) * (1.0f - a);
            r += wv * lut[px[o.r]];
            g += wv * lut[px[o.g]];
            b += wv * lut[px[o.b]];
            wsum += wv;
          }
        }
        num[0].row(y)[x] = r;
        num[1].row(y)[x] = g;
        num[2].row(y)[x] = b;
        wgt->row(y)[x] = wsum;
      }
  });
  if (!done) return failure(cancelledError());
  // Near the edge: a small window; deep inside the subject: wide, so every
  // cell finds some background to borrow from.
  auto wn = blurLarge(*wgt, 3.0f, token), ww = blurLarge(*wgt, 14.0f, token);
  if (!wn) return failure(wn.error());
  if (!ww) return failure(ww.error());
  for (int c = 0; c < 3; ++c) {
    auto bn = blurLarge(num[c], 3.0f, token), bw = blurLarge(num[c], 14.0f, token);
    if (!bn) return failure(bn.error());
    if (!bw) return failure(bw.error());
    for (size_t i = 0; i < num[c].size(); ++i) {
      const float sn = wn->data()[i], sw = ww->data()[i];
      num[c].data()[i] = sn > 0.05f ? bn->data()[i] / sn : sw > 1e-4f ? bw->data()[i] / sw : 0.0f;
    }
    out.c[c] = std::move(num[c]);
  }
  return out;
}

}  // namespace

Error apply(ImageBuffer& image, const ImageBuffer& subject, const ImageBuffer* faces, const ImageBuffer* protect,
            const ImageBuffer* depth, const Params& params, const CancellationToken* token, Report* report) {
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
  if (p.smooth >= 0.0f) p.smooth = std::min(p.smooth, 1.0f);
  if (p.blemish >= 0.0f) p.blemish = std::min(p.blemish, 1.0f);
  if (p.even >= 0.0f) p.even = std::min(p.even, 1.0f);
  p.bgLight = std::clamp(p.bgLight, -1.0f, 1.0f);
  p.bgColor = std::clamp(p.bgColor, -1.0f, 1.0f);
  p.edge = std::clamp(p.edge, 0.0f, 1.0f);
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
    // Edge refinement narrows the window (finer strands survive) and solves
    // with the colour guide, which separates hair from a background of
    // similar brightness but another hue.
    const auto radius = std::max<int32_t>(3, static_cast<int32_t>(shortSide / (150.0f + 110.0f * p.edge)));
    // Coefficients are smooth, so a grid of ~1024 px is plenty; the per-pixel
    // a·I + b evaluation restores full-resolution edges.
    const int32_t s = std::max<int32_t>({1, radius / 4, (std::max(W, H) + 1023) / 1024});
    const bool colourMatte = p.edge > 0.0f;
    auto guide = gridYcc(image, full, s, colourMatte, token);
    if (!guide) return guide.error();
    auto coarse = maskPlane(subject);
    if (!coarse) return coarse.error();
    auto pl = resizeBilinear(*coarse, guide->y.width(), guide->y.height(), token);
    if (!pl) return pl.error();
    auto a = Plane::allocate(guide->y.width(), guide->y.height());
    auto b = Plane::allocate(guide->y.width(), guide->y.height());
    if (!a || !b) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
    const int32_t rl = std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<float>(radius) / static_cast<float>(s))));
    if (colourMatte) {
      const int32_t gw = guide->y.width(), gh = guide->y.height();
      std::array<Plane, 3> rgb;
      for (auto& c : rgb) {
        auto q = Plane::allocate(gw, gh);
        if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
        c = std::move(q).value();
      }
      for (size_t i = 0; i < guide->y.size(); ++i) {
        yccToRgb(guide->y.data()[i], guide->cb.data()[i], guide->cr.data()[i], rgb[0].data()[i], rgb[1].data()[i], rgb[2].data()[i]);
      }
      std::array<Plane, 3> ac;
      for (auto& c : ac) {
        auto q = Plane::allocate(gw, gh);
        if (!q) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the cut-out");
        c = std::move(q).value();
      }
      // Free the luma-only buffers before the larger colour solve.
      a = Plane::allocate(1, 1);
      if (Error e = guidedCoefficientsColor({&rgb[0], &rgb[1], &rgb[2]}, *pl, rl, 1e-4f, ac, *b, token); e.status != PP_OK) return e;
      matte.color = true;
      matte.ac = std::move(ac);
    } else {
      if (Error e = guidedCoefficients(guide->y, *pl, rl, 2e-4f, *a, *b, token); e.status != PP_OK) return e;
      matte.a = std::move(a).value();
    }
    matte.b = std::move(b).value();
    matte.inv = 1.0f / static_cast<float>(s);
  }

  // 2. Skin (on a face-sized grid).
  SkinResult skin;
  if (faces != nullptr && !faces->empty() && p.skinWork()) {
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

  // 4. Depth-aware lens blur.
  const float blurRadius = p.blur * 0.04f * shortSide;
  std::optional<Bokeh> bokeh;
  DepthInfo depthInfo;
  if (blurRadius >= 1.5f) {
    std::optional<Plane> depthPlane;
    if (depth != nullptr && !depth->empty()) {
      auto dp = maskPlane(*depth);
      if (!dp) return dp.error();
      depthPlane = std::move(dp).value();
    }
    auto b = depthBokeh(image, matte, depthPlane ? &*depthPlane : nullptr, blurRadius, &depthInfo, token);
    if (!b) return b.error();
    bokeh = std::move(b).value();
  }

  // 5. One streaming pass: retouch (frequency separation), clarity, lift,
  //    vibrance, and the linear-light composite along the matte.
  const Rgb o = offsets(image);
  // Matched (slightly lighter) grain keeps blurred areas from looking
  // plastic next to the real subject; capped so results stay clean.
  const float grainAmt = bokeh ? std::min(0.6f * grain, 0.008f) : 0.0f;
  // Selective background light and colour, and edge decontamination (only
  // where the background changes: otherwise the edge already matches it).
  const bool bgAdjust = p.bgLight != 0.0f || p.bgColor != 0.0f;
  const bool decon = p.edge > 0.0f && (bokeh.has_value() || bgAdjust);
  std::optional<BackgroundEstimate> bgEst;
  if (decon) {
    auto e = backgroundEstimate(image, matte, shortSide, token);
    if (!e) return e.error();
    bgEst = std::move(e).value();
  }
  const float bgGain = std::exp2(p.bgLight);
  const float bgSat = p.bgColor < 0.0f ? 1.0f + p.bgColor : 1.0f + 0.6f * p.bgColor;
  std::vector<double> rowCleaned(static_cast<size_t>(H), 0.0);
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
        const float a = matte.at(x, y, px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f);
        cov += a;
        float yv = y0, sv = 0.0f;
        if (skin.active() && skin.rect.contains(x, y)) {
          const float inv = 1.0f / static_cast<float>(skin.scale);
          const float gx = (static_cast<float>(x - skin.rect.x0) + 0.5f) * inv - 0.5f;
          const float gy = (static_cast<float>(y - skin.rect.y0) + 0.5f) * inv - 0.5f;
          sv = skin.skin.sample(gx, gy);
          if (sv > 0.0f) {
            const float base = skin.scale == 1 ? y0 : skin.base.sample(gx, gy);
            // Fine texture finer than the grid is softened, but strong local
            // contrast (lash line, nostril, lip edge) is a feature: keep it.
            const float fine = y0 - base;
            const float soften = 1.0f - smoothstep(0.12f * skin.skinLuma, 0.3f * skin.skinLuma, std::fabs(fine));
            yv = y0 + sv * ((skin.retouched.sample(gx, gy) - base) + (skin.keep - 1.0f) * fine * soften);
            // Grid colour changes follow this pixel's own brightness, so a
            // dark feature inside a skin cell is never tinted.
            const float cscale = std::clamp((y0 + 0.02f) / (base + 0.02f), 0.0f, 1.5f);
            cb += sv * cscale * skin.dcb.sample(gx, gy);
            cr += sv * cscale * skin.dcr.sample(gx, gy);
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
          // Exposure-style lift (relative, so deep and light tones brighten
          // alike); chroma follows luma to keep saturation. Clarity can push
          // a near-black pixel below zero, so luma is clamped first and the
          // ratio is bounded: a ratio of near-zero values would turn the
          // darkest features (nostrils, lashes) a false colour.
          yv = std::max(yv, 0.0f);
          const float before = yv;
          yv = std::min(1.0f, yv * (1.0f + p.brighten * 0.045f * a * (1.0f - yv) * smoothstep(0.02f, 0.25f, yv)));
          const float k = std::clamp((yv + 0.02f) / (before + 0.02f), 0.0f, 1.25f);
          cb *= k;
          cr *= k;
        }
        float r, g, b;
        yccToRgb(yv, cb, cr, r, g, b);
        if (bgEst && a > 0.01f && a < 0.99f) {
          // Edge pixels mix subject and old background: I = aF + (1-a)B.
          // Recover F (in linear light) so the new background shows no fringe.
          const float ar = std::max(matte.raw(x, y, px[o.r] / 255.0f, px[o.g] / 255.0f, px[o.b] / 255.0f), 0.35f);
          const float f = static_cast<float>(bgEst->factor);
          const float ex = (static_cast<float>(x) + 0.5f) / f - 0.5f, ey = (static_cast<float>(y) + 0.5f) / f - 0.5f;
          const float wv = p.edge * (1.0f - smoothstep(0.85f, 0.99f, a));
          auto clean = [&](float c, const Plane& B) {
            const float L = toLinear(c);
            const float F = std::clamp((L - (1.0f - ar) * B.sample(ex, ey)) / ar, 0.0f, 1.0f);
            return toGamma(L + wv * (F - L));
          };
          r = clean(r, bgEst->c[0]);
          g = clean(g, bgEst->c[1]);
          b = clean(b, bgEst->c[2]);
          rowCleaned[static_cast<size_t>(y)] += 1.0;
        }
        if (bokeh) {
          // Back to front: far layer, then the sharp in-focus slab (subject and
          // whatever shares its depth), then anything nearer than the subject.
          const float d = static_cast<float>(bokeh->factor);
          const float bx = (static_cast<float>(x) + 0.5f) / d - 0.5f, by = (static_cast<float>(y) + 0.5f) / d - 0.5f;
          const float sharp = std::max(a, bokeh->focus.sample(bx, by));
          const float na = std::min(1.0f, bokeh->nearA.sample(bx, by)) * (1.0f - a);
          const float soft = 1.0f - sharp * (1.0f - na);
          const float n = grainAmt * soft * hashNoise(x, y, 0x5EEDu);
          auto layer = [&](float c, const Plane& farP, const Plane& nearP) {
            const float base = sharp * toLinear(c) + (1.0f - sharp) * farP.sample(bx, by);
            return toGamma((1.0f - a) * nearP.sample(bx, by) + (1.0f - na) * base) + n;
          };
          r = layer(r, bokeh->farR, bokeh->nearR);
          g = layer(g, bokeh->farG, bokeh->nearG);
          b = layer(b, bokeh->farB, bokeh->nearB);
        }
        if (bgAdjust && a < 1.0f) {
          // Everything that is not the subject: light in stops (highlights
          // roll off instead of clipping) and colour around its own luminance.
          float lr = toLinear(r), lg = toLinear(g), lb = toLinear(b);
          auto tone = [&](float v) { return bgGain > 1.0f ? v * bgGain / (1.0f + v * (bgGain - 1.0f)) : v * bgGain; };
          lr = tone(lr);
          lg = tone(lg);
          lb = tone(lb);
          const float Y = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
          lr = std::clamp(Y + bgSat * (lr - Y), 0.0f, 1.0f);
          lg = std::clamp(Y + bgSat * (lg - Y), 0.0f, 1.0f);
          lb = std::clamp(Y + bgSat * (lb - Y), 0.0f, 1.0f);
          const float wbg = 1.0f - a;
          r += wbg * (toGamma(lr) - r);
          g += wbg * (toGamma(lg) - g);
          b += wbg * (toGamma(lb) - b);
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
    report->focusDepth = depthInfo.focus;
    report->depthEstimated = depthInfo.estimated;
    double cleaned = 0.0;
    for (double c : rowCleaned) cleaned += c;
    report->edgeCleaned = static_cast<float>(cleaned / n);
  }
  return ok();
}

}  // namespace photoplus::portrait
