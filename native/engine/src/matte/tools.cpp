// Matte tools: see tools.hpp.
#include "tools.hpp"

#include <utility>
#include <vector>

namespace photoplus::matte {

Result<Rgb3> allocRgb(int32_t w, int32_t h) {
  auto r = Plane::allocate(w, h), g = Plane::allocate(w, h), b = Plane::allocate(w, h);
  if (!r || !g || !b) return failure(makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop"));
  return Rgb3{std::move(r).value(), std::move(g).value(), std::move(b).value()};
}

// A Gaussian along one axis only (three running boxes), for backgrounds
// with lines in them: a seam, a shelf, a horizon.
Result<Plane> blurredAlong(const Plane& src, float sigma, bool horizontal, const CancellationToken* token) {
  const int32_t w = src.width(), h = src.height();
  auto out = Plane::allocate(w, h);
  if (!out) return failure(out.error());
  std::copy(src.data(), src.data() + src.size(), out->data());
  const int32_t r = std::max<int32_t>(1, static_cast<int32_t>(std::lround(0.5f * (std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f))));
  const int32_t lines = horizontal ? h : w, len = horizontal ? w : h;
  const bool done = parallelFor(lines, token, [&](int32_t begin, int32_t end) {
    std::vector<float> a(static_cast<size_t>(len)), b(static_cast<size_t>(len));
    for (int32_t l = begin; l < end; ++l) {
      auto at = [&](int32_t k) -> float& { return horizontal ? out->row(l)[k] : out->row(k)[l]; };
      for (int32_t k = 0; k < len; ++k) a[static_cast<size_t>(k)] = at(k);
      for (int pass = 0; pass < 3; ++pass) {
        double acc = 0;
        int32_t n = 0;
        for (int32_t k = 0; k <= std::min(r, len - 1); ++k) acc += a[static_cast<size_t>(k)], ++n;
        for (int32_t k = 0; k < len; ++k) {
          b[static_cast<size_t>(k)] = static_cast<float>(acc / n);
          const int32_t add = k + r + 1, sub = k - r;
          if (add < len) acc += a[static_cast<size_t>(add)], ++n;
          if (sub >= 0) acc -= a[static_cast<size_t>(sub)], --n;
        }
        std::swap(a, b);
      }
      for (int32_t k = 0; k < len; ++k) at(k) = a[static_cast<size_t>(k)];
    }
  });
  if (!done) return failure(cancelledError());
  return out;
}

// A Gaussian blur. Wide ones (most here: plates, reach, glow) run as three
// running boxes per axis, whose cost does not grow with the width.
Result<Plane> blurred(const Plane& src, float sigma, const CancellationToken* token) {
  if (sigma >= 3.0f) {
    auto h = blurredAlong(src, sigma, true, token);
    if (!h) return h;
    return blurredAlong(*h, sigma, false, token);
  }
  auto out = Plane::allocate(src.width(), src.height());
  if (!out) return failure(out.error());
  if (Error e = gaussianBlur(src, *out, sigma, token); e.status != PP_OK) return failure(e);
  return out;
}

// The background blurred without the subject: blur(I·(1-a)) / blur(1-a).
// Normalised convolution: the colour of `src` where `weight` is high,
// spread smoothly over the frame (or along rows or columns: axis 1 / 2).
// Far from any weighted pixel, `fallback`.
Result<Rgb3> weightedPlate(const Rgb3& src, const Plane& weight, float sigma, V3 fallback, const CancellationToken* token,
                           int axis, const Rgb3* fallbackPlate) {
  const int32_t w = weight.width(), h = weight.height();
  auto blur = [&](const Plane& p) { return axis == 0 ? blurred(p, sigma, token) : blurredAlong(p, sigma, axis == 1, token); };
  auto wsum = blur(weight);
  if (!wsum) return failure(wsum.error());
  auto tmp = Plane::allocate(w, h);
  if (!tmp) return failure(tmp.error());
  auto out = allocRgb(w, h);
  if (!out) return out;
  const Plane* in[3] = {&src.r, &src.g, &src.b};
  Plane* dst[3] = {&out->r, &out->g, &out->b};
  const float fb[3] = {fallback.r, fallback.g, fallback.b};
  for (int c = 0; c < 3; ++c) {
    for (size_t i = 0; i < weight.size(); ++i) tmp->data()[i] = in[c]->data()[i] * weight.data()[i];
    auto b = blur(*tmp);
    if (!b) return failure(b.error());
    for (size_t i = 0; i < weight.size(); ++i) {
      const float wv = wsum->data()[i];
      const float k = smoothstep(0.0f, 0.05f, wv);
      const float far = fallbackPlate != nullptr ? (c == 0 ? fallbackPlate->r : c == 1 ? fallbackPlate->g : fallbackPlate->b).data()[i] : fb[c];
      dst[c]->data()[i] = k * (b->data()[i] / std::max(wv, 1e-4f)) + (1.0f - k) * far;
    }
  }
  return out;
}

// The background without the subject: nothing of the person bleeds into it.
Result<Rgb3> cleanPlate(const Rgb3& low, const Plane& alpha, float sigma, V3 fallback, const CancellationToken* token) {
  auto wv = Plane::allocate(alpha.width(), alpha.height());
  if (!wv) return failure(wv.error());
  for (size_t i = 0; i < alpha.size(); ++i) wv->data()[i] = 1.0f - alpha.data()[i];
  return weightedPlate(low, *wv, sigma, fallback, token);
}

// Hair the segmenter missed. Person masks stop short of soft or curly hair
// (and sometimes notch the crown); a repainted background then cuts the head
// along that line. Around the mask, each pixel is compared with the nearby
// person colour F (just inside the mask: hair at the top of the head) and the
// real background B (well outside it): alpha = (I-B)·(F-B) / |F-B|². Where F
// and B are too alike to tell apart, the mask stands.
Error recoverHair(const Rgb3& gam, Plane& mask, const CancellationToken* token) {
  const int32_t w = mask.width(), h = mask.height();
  const float shortSide = static_cast<float>(std::min(w, h));
  auto reach = blurred(mask, 0.016f * shortSide, token);
  auto inner = blurred(mask, 0.008f * shortSide, token);
  if (!reach || !inner) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  auto wF = Plane::allocate(w, h), wB = Plane::allocate(w, h);
  if (!wF || !wB) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  for (size_t i = 0; i < mask.size(); ++i) {
    wF->data()[i] = smoothstep(0.88f, 0.99f, inner->data()[i]);
    wB->data()[i] = 1.0f - smoothstep(0.004f, 0.03f, reach->data()[i]);
  }
  // Texture: hair (curls, strands, flyaways) is busy at fine scale; a wall's
  // seam or shadow next to the head is smooth. Each pixel's fine detail is
  // compared with that of the person just inside the mask.
  auto luma = Plane::allocate(w, h);
  if (!luma) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  for (size_t i = 0; i < mask.size(); ++i) luma->data()[i] = 0.299f * gam.r.data()[i] + 0.587f * gam.g.data()[i] + 0.114f * gam.b.data()[i];
  auto fine = blurred(*luma, 1.0f, token);
  if (!fine) return fine.error();
  for (size_t i = 0; i < mask.size(); ++i) fine->data()[i] = std::fabs(luma->data()[i] - fine->data()[i]);
  auto tex = blurred(*fine, std::max(2.0f, 0.01f * shortSide), token);
  if (!tex) return tex.error();
  for (size_t i = 0; i < mask.size(); ++i) fine->data()[i] = tex->data()[i] * wF->data()[i];
  auto texNum = blurred(*fine, 0.02f * shortSide, token);
  auto texDen = blurred(*wF, 0.02f * shortSide, token);
  if (!texNum || !texDen) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  // Each pixel is judged on its colour softened a little: strands of fair
  // hair can be as dark, one by one, as the wall behind them.
  const float judgeSigma = std::max(1.0f, 0.004f * shortSide);
  auto sr = blurred(gam.r, judgeSigma, token), sg = blurred(gam.g, judgeSigma, token), sb = blurred(gam.b, judgeSigma, token);
  if (!sr || !sg || !sb) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  const Rgb3 judged{std::move(sr).value(), std::move(sg).value(), std::move(sb).value()};
  // The person's colour near each pixel; further out, a wider view of it.
  auto Fwide = weightedPlate(gam, *wF, 0.07f * shortSide, grey(0.2f), token);
  if (!Fwide) return Fwide.error();
  auto F = weightedPlate(gam, *wF, 0.02f * shortSide, grey(0.2f), token, 0, &*Fwide);
  // Three views of the background: the neighbourhood, the same row and the
  // same column. A pixel is hair only if none of them explains it — a seam
  // or shelf running behind the head is explained by its own row.
  auto B = weightedPlate(gam, *wB, 0.045f * shortSide, grey(0.5f), token);
  auto Bh = weightedPlate(gam, *wB, 0.12f * shortSide, grey(0.5f), token, 1);
  auto Bv = weightedPlate(gam, *wB, 0.12f * shortSide, grey(0.5f), token, 2);
  if (!F || !B || !Bh || !Bv) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  // How much a pixel looks like F rather than background b.
  auto hairness = [](V3 c, V3 f, V3 b) {
    const V3 d = f - b, e = c - b;
    const float dd = d.r * d.r + d.g * d.g + d.b * d.b;
    const float sep = std::sqrt(dd);
    const float conf = smoothstep(0.06f, 0.16f, sep);
    if (conf <= 0.0f) return 0.0f;
    const float k = (e.r * d.r + e.g * d.g + e.b * d.b) / std::max(dd, 1e-6f);
    // How far the pixel strays from the F–B line: a third colour (a lamp,
    // a plant) is neither, so it is not hair.
    const V3 off = e - d * std::clamp(k, 0.0f, 1.0f);
    const float stray = std::sqrt(off.r * off.r + off.g * off.g + off.b * off.b) / std::max(sep, 1e-3f);
    return smoothstep(0.25f, 0.8f, k) * (1.0f - smoothstep(0.35f, 0.7f, stray)) * conf;
  };
  // Holes in the silhouette (a notch at the crown) are almost always the
  // mask's mistake. A hole is a gap with the person on both sides — left and
  // right, or above and below — close by; the space beside a head is not.
  auto enclosed = Plane::allocate(w, h);
  if (!enclosed) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  std::fill(enclosed->data(), enclosed->data() + enclosed->size(), 0.0f);
  {
    const float R = 0.07f * shortSide;
    auto gapScore = [&](float before, float after) { return 1.0f - smoothstep(0.5f * R, R, std::max(before, after)); };
    const float none = 1e9f;
    const bool rows = parallelFor(h, token, [&](int32_t begin, int32_t end) {
      std::vector<float> left(static_cast<size_t>(w));
      for (int32_t y = begin; y < end; ++y) {
        const float* m = mask.row(y);
        float last = -none;
        for (int32_t x = 0; x < w; ++x) {
          if (m[x] > 0.5f) last = static_cast<float>(x);
          left[static_cast<size_t>(x)] = static_cast<float>(x) - last;
        }
        float next = none;
        for (int32_t x = w - 1; x >= 0; --x) {
          if (m[x] > 0.5f) next = static_cast<float>(x);
          enclosed->row(y)[x] = gapScore(left[static_cast<size_t>(x)], next - static_cast<float>(x));
        }
      }
    });
    if (!rows) return cancelledError();
    const bool cols = parallelFor(w, token, [&](int32_t begin, int32_t end) {
      std::vector<float> up(static_cast<size_t>(h));
      for (int32_t x = begin; x < end; ++x) {
        float last = -none;
        for (int32_t y = 0; y < h; ++y) {
          if (mask.row(y)[x] > 0.5f) last = static_cast<float>(y);
          up[static_cast<size_t>(y)] = static_cast<float>(y) - last;
        }
        float next = none;
        for (int32_t y = h - 1; y >= 0; --y) {
          if (mask.row(y)[x] > 0.5f) next = static_cast<float>(y);
          float& e = enclosed->row(y)[x];
          e = std::max(e, gapScore(up[static_cast<size_t>(y)], next - static_cast<float>(y)));
        }
      }
    });
    if (!cols) return cancelledError();
  }
  // Texture, judged against the person's own.
  auto busyAt = [&](size_t i) {
    const float texIn = texNum->data()[i] / std::max(texDen->data()[i], 1e-4f);
    return smoothstep(0.2f, 0.5f, tex->data()[i] / std::max(texIn, 0.006f));
  };
  // 1. Holes, judged by the neighbourhood view of the background.
  auto add = Plane::allocate(w, h);
  if (!add) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  const bool filled = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        const float m = mask.data()[i], hole = enclosed->data()[i];
        float& out = add->data()[i];
        out = m;
        if (hole <= 0.0f || m >= 0.999f) continue;
        out = std::max(m, hole * hairness(judged.at(i), F->at(i), B->at(i)) * busyAt(i));
      }
  });
  if (!filled) return cancelledError();
  // 2. Hair beyond the (now whole) silhouette, up to ~3% of the short side:
  //    hair only if no view of the background explains the pixel.
  auto reach2 = blurred(*add, 0.016f * shortSide, token);
  if (!reach2) return reach2.error();
  const bool done = parallelFor(h, token, [&](int32_t begin, int32_t end) {
    for (int32_t y = begin; y < end; ++y)
      for (int32_t x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        float& out = add->data()[i];
        const float band = smoothstep(0.02f, 0.12f, reach2->data()[i]);
        if (band <= 0.0f || out >= 0.999f) continue;
        const V3 f = F->at(i), c = judged.at(i);
        const float hv = hairness(c, f, B->at(i));
        if (hv <= 0.0f) continue;
        const float strict = std::min({hv, hairness(c, f, Bh->at(i)), hairness(c, f, Bv->at(i))});
        out = std::max(out, strict * band * busyAt(i));
      }
  });
  if (!done) return cancelledError();
  // Hair is a soft layer round the head; a background line wrongly taken
  // for it is a thin spike off the silhouette. An opening (erode, then
  // grow back) at the hair's own scale removes spikes and keeps the layer.
  const float openSigma = std::max(1.0f, 0.006f * shortSide);
  auto firm = Plane::allocate(w, h);
  if (!firm) return makeError(PP_ERROR_BUDGET_EXCEEDED, "Not enough memory for the backdrop");
  for (size_t i = 0; i < mask.size(); ++i) firm->data()[i] = smoothstep(0.3f, 0.6f, add->data()[i]);
  auto eroded = blurred(*firm, openSigma, token);
  if (!eroded) return eroded.error();
  for (size_t i = 0; i < mask.size(); ++i) eroded->data()[i] = smoothstep(0.85f, 0.97f, eroded->data()[i]);
  auto regrown = blurred(*eroded, openSigma, token);
  if (!regrown) return regrown.error();
  for (size_t i = 0; i < mask.size(); ++i) {
    const float opened = smoothstep(0.03f, 0.15f, regrown->data()[i]);
    mask.data()[i] = std::max(mask.data()[i], std::min(add->data()[i], opened));
  }
  return ok();
}

// Stray islands. A segmenter now and then marks a patch of the background
// (a bright wall corner, a lamp) as person, often for a single video frame;
// cut out and re-lit, it would pop in and out of the new scene. Patches of
// "person" not connected to anyone (smaller than 8% of the largest) are
// dropped. People apart from each other are each large, and stay.
Error dropIslands(Plane& mask, const CancellationToken* token) {
  const int32_t w = mask.width(), h = mask.height();
  std::vector<int32_t> label(mask.size(), -1);
  std::vector<int64_t> area;
  std::vector<int32_t> stack;
  for (int32_t y0 = 0; y0 < h; ++y0) {
    if (isCancelled(token)) return cancelledError();
    for (int32_t x0 = 0; x0 < w; ++x0) {
      const size_t i0 = static_cast<size_t>(y0) * static_cast<size_t>(w) + static_cast<size_t>(x0);
      if (label[i0] >= 0 || mask.data()[i0] <= 0.5f) continue;
      const auto id = static_cast<int32_t>(area.size());
      int64_t n = 0;
      label[i0] = id;
      stack.assign(1, static_cast<int32_t>(i0));
      while (!stack.empty()) {
        const int32_t i = stack.back();
        stack.pop_back();
        ++n;
        const int32_t x = i % w, y = i / w;
        const int32_t nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
        for (const auto& q : nb) {
          if (q[0] < 0 || q[1] < 0 || q[0] >= w || q[1] >= h) continue;
          const int32_t j = q[1] * w + q[0];
          if (label[static_cast<size_t>(j)] >= 0 || mask.data()[static_cast<size_t>(j)] <= 0.5f) continue;
          label[static_cast<size_t>(j)] = id;
          stack.push_back(j);
        }
      }
      area.push_back(n);
    }
  }
  if (area.size() < 2) return ok();
  const int64_t largest = *std::max_element(area.begin(), area.end());
  auto drop = Plane::allocate(w, h);
  if (!drop) return drop.error();
  bool any = false;
  for (size_t i = 0; i < mask.size(); ++i) {
    const bool d = label[i] >= 0 && area[static_cast<size_t>(label[i])] * 100 < largest * 8;
    drop->data()[i] = d ? 1.0f : 0.0f;
    any = any || d;
  }
  if (!any) return ok();
  // Feathered, so the soft rim of a dropped patch goes with it.
  auto soft = blurred(*drop, 1.5f, token);
  if (!soft) return soft.error();
  for (size_t i = 0; i < mask.size(); ++i) {
    const bool kept = label[i] >= 0 && drop->data()[i] == 0.0f;
    if (!kept) mask.data()[i] *= 1.0f - smoothstep(0.02f, 0.25f, std::max(drop->data()[i], soft->data()[i]));
  }
  return ok();
}

}  // namespace photoplus::matte
