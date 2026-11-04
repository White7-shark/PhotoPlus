#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "photoplus/enhance.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::enhance {

namespace {

struct FrameData {
  Plane y;
  Plane cbHalf;  // chroma kept at half resolution (4:2:0-like) to save memory
  Plane crHalf;
};

Result<FrameData> prepare(const ImageBuffer& img, const CancellationToken* token) {
  auto ycc = toYcc(img, token);
  if (!ycc) return failure(ycc.error());
  auto cb = downsample2(ycc->cb, token);
  if (!cb) return failure(cb.error());
  auto cr = downsample2(ycc->cr, token);
  if (!cr) return failure(cr.error());
  return FrameData{std::move(ycc->y), std::move(cb).value(), std::move(cr).value()};
}

}  // namespace

int32_t outputWidth(const ImageBuffer& frame, const Params& params) { return frame.width() * params.upscale; }
int32_t outputHeight(const ImageBuffer& frame, const Params& params) { return frame.height() * params.upscale; }

Result<ImageBuffer> run(const std::vector<const ImageBuffer*>& frames, const Params& params,
                        const CancellationToken* token, Report* report, ImageBuffer* destination) {
  ScopedTimer timer("enh.total");
  if (frames.empty() || frames.size() > 16) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Enhancement needs between 1 and 16 frames"));
  }
  if (params.upscale != 1 && params.upscale != 2) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "upscale must be 1 or 2"));
  }
  for (const ImageBuffer* f : frames) {
    if (f == nullptr || f->empty()) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Null frame"));
    if (f->width() != frames[0]->width() || f->height() != frames[0]->height()) {
      return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "All burst frames must have the same size"));
    }
  }
  const int32_t scale = params.upscale;
  const int32_t w = frames[0]->width(), h = frames[0]->height();
  if (w < 16 || h < 16) return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Image is too small to enhance"));
  if (destination != nullptr &&
      (destination->width() != w * scale || destination->height() != h * scale ||
       bytesPerPixel(destination->format()) != 4)) {
    return failure(makeError(PP_ERROR_INVALID_ARGUMENT, "Destination must be 4-channel and match the output size"));
  }
  Report rep;
  rep.framesInput = static_cast<int32_t>(frames.size());

  // 1. Working representation for every frame.
  std::vector<FrameData> data;
  data.reserve(frames.size());
  for (const ImageBuffer* f : frames) {
    auto d = prepare(*f, token);
    if (!d) return failure(d.error());
    data.push_back(std::move(d).value());
  }

  // 2. Reference frame. Frame 0 is the shutter-press moment (what the user
  //    framed), so it stays the anchor unless another frame is clearly
  //    sharper ("lucky imaging": hand-shake blurs some frames more than
  //    others). Sharpness is judged at half resolution so noise doesn't win.
  size_t refIdx = 0;
  if (data.size() > 1) {
    std::vector<float> sharp(data.size(), 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
      auto half = downsample2(data[i].y, token);
      if (!half) return failure(half.error());
      sharp[i] = laplacianSharpness(*half);
    }
    for (size_t i = 1; i < data.size(); ++i) {
      if (sharp[i] > sharp[refIdx] && sharp[i] > 1.25f * sharp[0]) refIdx = i;
    }
    std::swap(data[0], data[refIdx]);
  }
  rep.referenceIndex = static_cast<int32_t>(refIdx);

  auto quality = analyze(data[0].y, token);
  if (!quality) return failure(quality.error());
  rep.noiseBefore = quality->noiseSigma;
  rep.blurBefore = quality->blurSigma;

  // 3. Align every other frame to the reference; reject frames that don't fit.
  std::vector<Shift> shifts(data.size());
  shifts[0].valid = true;
  int32_t used = 1;
  const float maxResidual = 0.03f + 6.0f * quality->noiseSigma;
  for (size_t i = 1; i < data.size(); ++i) {
    auto s = alignTranslation(data[0].y, data[i].y, token);
    if (!s) return failure(s.error());
    shifts[i] = *s;
    if (shifts[i].valid && shifts[i].residual > maxResidual) shifts[i].valid = false;
    if (shifts[i].valid) ++used;
  }
  rep.framesUsed = used;

  // 4. Fuse luma (burst denoise or multi-frame super-resolution) and chroma.
  const int32_t ow = w * scale, oh = h * scale;
  YccImage out;
  bool superResolved = false;
  {
    std::vector<const Plane*> ys;
    for (const auto& d : data) ys.push_back(&d.y);
    if (used > 1) {
      // Narrow kernel when super-resolving: back-projection refines it.
      auto fused = fuse(ys, shifts, scale, quality->noiseSigma, token, scale == 2 ? 0.5f : 0.0f);
      if (!fused) return failure(fused.error());
      if (scale == 2 && params.sharpen > 0.0f) {
        auto maps = robustnessMaps(ys, shifts, quality->noiseSigma, token);
        if (!maps) return failure(maps.error());
        std::vector<const Plane*> weights;
        for (const auto& m : *maps) weights.push_back(m.empty() ? nullptr : &m);
        // Optical blur in output pixels (the sensor's pixel integration is
        // modelled separately by the back-projection).
        const float lrBlur = quality->blurSigma > 0.0f ? quality->blurSigma : 0.6f;
        const float psf = std::clamp(2.0f * std::sqrt(std::max(lrBlur * lrBlur - 0.12f, 0.2f)), 0.8f, 3.0f);
        const int32_t iters = static_cast<int64_t>(ow) * oh > 8'000'000 ? 12 : 20;
        auto sr = superResolve(ys, shifts, weights, std::move(fused).value(), scale, psf, iters, token);
        if (!sr) return failure(sr.error());
        out.y = std::move(sr).value();
        superResolved = true;
        rep.deblurSigma = psf;
      } else {
        out.y = std::move(fused).value();
      }
    } else if (scale == 2) {
      auto up = resizeCubic(data[0].y, ow, oh, token);
      if (!up) return failure(up.error());
      out.y = std::move(up).value();
    } else {
      out.y = std::move(data[0].y);
    }
    // Chroma: fuse on the half-res grid (shifts halve too), then upsample.
    std::vector<Shift> half = shifts;
    for (auto& s : half) {
      s.dx *= 0.5f;
      s.dy *= 0.5f;
    }
    auto fuseChroma = [&](auto member) -> Result<Plane> {
      std::vector<const Plane*> cs;
      for (const auto& d : data) cs.push_back(&(d.*member));
      Plane merged;
      if (used > 1) {
        auto m = fuse(cs, half, 1, quality->noiseSigma, token);
        if (!m) return failure(m.error());
        merged = std::move(m).value();
      } else {
        auto c = clonePlane(*cs[0]);
        if (!c) return failure(c.error());
        merged = std::move(c).value();
      }
      return resizeCubic(merged, ow, oh, token);
    };
    auto cb = fuseChroma(&FrameData::cbHalf);
    if (!cb) return failure(cb.error());
    auto cr = fuseChroma(&FrameData::crHalf);
    if (!cr) return failure(cr.error());
    out.cb = std::move(cb).value();
    out.cr = std::move(cr).value();
  }
  data.clear();  // release burst memory before the heavy restoration passes

  // 5–6. Restore. Measured on synthetic weak-camera captures (see
  //      tests/enhance_tests.cpp and tools/enhance_bench): a gentle
  //      edge-preserving denoise followed by noise-regularised deconvolution
  //      beats a strong denoise, which erases the detail deblurring needs.
  float residualNoise = quality->noiseSigma / std::sqrt(static_cast<float>(used));
  float measuredBlur = quality->blurSigma > 0.0f ? quality->blurSigma : 0.6f;
  if (used > 1 && scale == 1) {
    // Re-measure on the merged image: far less noise means a more reliable
    // blur estimate, and it includes the merge's own slight softening.
    residualNoise = estimateNoise(out.y);
    auto fusedBlur = estimateBlurSigma(out.y, residualNoise, token);
    if (!fusedBlur) return failure(fusedBlur.error());
    if (*fusedBlur > 0.0f) measuredBlur = *fusedBlur;
  }
  rep.noiseAfter = residualNoise;
  // A sharp, well-focused capture still measures ~0.4 px; only the excess is
  // treated as removable optical blur.
  const float optical = std::sqrt(std::max(measuredBlur * measuredBlur - 0.16f, 0.04f));
  const bool willDeblur = params.sharpen > 0.0f && !superResolved;

  if (params.denoise > 0.0f && residualNoise > 0.002f) {
    // When deconvolution follows, its TV term also suppresses noise, so the
    // pre-denoise backs off for blurry captures.
    const float k = 1.5f * std::clamp(params.denoise, 0.0f, 1.0f) * (willDeblur && optical >= 1.2f ? 0.5f : 1.0f);
    const float eps = (k * residualNoise) * (k * residualNoise);
    if (eps > 0.0f) {
      if (Error e = denoiseGuided(out.y, scale == 2 ? 3 : 2, eps, 0.12f, token); e.status != PP_OK) return failure(e);
    }
    // Chroma noise is blotchy and low-frequency; smooth it much harder.
    const float ceps = std::max(eps, (1.5f * residualNoise) * (1.5f * residualNoise)) * 6.0f + 1e-5f;
    if (Error e = denoiseGuided(out.cb, 4 * scale, ceps, 0.0f, token); e.status != PP_OK) return failure(e);
    if (Error e = denoiseGuided(out.cr, 4 * scale, ceps, 0.0f, token); e.status != PP_OK) return failure(e);
  }

  if (willDeblur) {
    const float resample = scale == 2 ? 0.6f : 0.0f;
    const float s = static_cast<float>(scale);
    const float psf = std::min(std::sqrt(optical * s * optical * s + resample * resample), 4.0f * s);
    const auto iterations = static_cast<int32_t>(std::lround(6.0f + 14.0f * std::clamp(params.sharpen, 0.0f, 1.0f)));
    const float tv = 0.15f * residualNoise;  // regularise only as much as the noise requires
    rep.deblurSigma = psf;
    if (Error e = deconvolve(out.y, psf, iterations, tv, token); e.status != PP_OK) return failure(e);
  }

  // 7. Tone.
  ToneParams tp;
  tp.autoWhiteBalance = params.autoTone;
  tp.autoLevels = params.autoTone;
  tp.localContrast = params.detail;
  tp.vibrance = params.vibrance;
  if (Error e = tone(out, tp, token); e.status != PP_OK) return failure(e);

  // 8. Back to 8-bit RGBA, straight into the caller's buffer when provided.
  if (destination != nullptr) {
    if (Error e = fromYcc(out, *destination, token); e.status != PP_OK) return failure(e);
    if (report != nullptr) *report = rep;
    return ImageBuffer{};
  }
  auto result = ImageBuffer::allocate(ow, oh, PixelFormat::RGBA8);
  if (!result) return failure(result.error());
  if (Error e = fromYcc(out, *result, token); e.status != PP_OK) return failure(e);
  if (report != nullptr) *report = rep;
  return std::move(result).value();
}

}  // namespace photoplus::enhance
