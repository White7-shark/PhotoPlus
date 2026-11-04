# Enhancement benchmark: weak camera, strong photos

**Claim under test:** a phone with a modest sensor and lens can still produce high-quality photos when software does the heavy lifting.

## Method

`scripts/enhance-benchmark.py` simulates a cheap phone camera from each ground-truth photo, runs the real Photo Plus C++ engine on the result (through `native/engine/tools/enhance_bench`), and scores the output against the original.

**Ground truth:** 8 images from the Kodak PhotoCD set (768×512), which Kodak released for unrestricted use. It is a standard benchmark for image restoration.

**Simulated camera.** Each frame of an 8-frame handheld burst gets:

- **Resolution:** half the linear resolution, so ¼ of the megapixels (384×256)
- **Lens:** soft, with a Gaussian blur of σ = 1.0 px at full resolution
- **Hand-shake:** random sub-pixel shifts of up to ±1.5 px
- **Sensor noise:** signal-dependent shot noise plus read noise (σ = 0.012)
- **Processing:** 8-bit quantisation and JPEG compression at quality 88, like a phone's image processor

Everything is compared at the original resolution (768×512). PSNR is measured on RGB and SSIM on luma, both excluding a 16 px border. Restoration runs without the auto-tone stage, so brightness and colour changes don't distort the scores.

## Results

| Image | Ordinary upscale (bicubic) | Photo Plus single-frame | **Photo Plus 8-frame burst** |
|---|---|---|---|
| kodim01 | 23.80 dB / 0.608 | 24.47 dB / 0.665 | **25.97 dB / 0.749** |
| kodim05 | 23.33 dB / 0.703 | 24.15 dB / 0.749 | **25.19 dB / 0.802** |
| kodim08 | 21.19 dB / 0.642 | 22.26 dB / 0.705 | **23.77 dB / 0.776** |
| kodim13 | 21.77 dB / 0.551 | 22.39 dB / 0.605 | **23.43 dB / 0.698** |
| kodim15 | 29.58 dB / 0.808 | 29.74 dB / 0.809 | **30.66 dB / 0.814** |
| kodim19 | 25.35 dB / 0.724 | 26.24 dB / 0.737 | **27.92 dB / 0.785** |
| kodim21 | 25.79 dB / 0.750 | 26.07 dB / 0.768 | **27.42 dB / 0.795** |
| kodim23 | 30.18 dB / 0.865 | 30.36 dB / 0.852 | **31.76 dB** / 0.849 |
| **Mean** | **25.12 dB / 0.706** | **25.71 dB / 0.736** | **27.02 dB / 0.783** |

Each cell is PSNR / SSIM; higher is better. Raw numbers are in `enhancement-results.json`.

- **Burst super-resolution beats the ordinary upscale on every image:** +1.9 dB PSNR and +0.077 SSIM on average, up to +2.6 dB on kodim19.
- **Single-frame restoration helps less** (+0.6 dB mean), because one frame can't contain information the sensor never captured. The burst can: hand-shake makes each frame sample the scene at different sub-pixel positions.
- **Known weakness:** on kodim23, a smooth, low-detail image, SSIM dips slightly (0.849 vs 0.865) even though PSNR rises by 1.6 dB. The restoration's deblurring leaves faint texture in flat areas. This is being tracked for tuning.

Visual comparisons: `compare-kodim05.png`, `compare-kodim13.png`, `compare-kodim23.png`. Each shows the weak capture, the ordinary upscale, the burst result, the full Enhance (burst plus auto tone), and the original, cropped 1:1.

## How it works (native/engine/src/enhance)

1. **Quality analysis.** Noise is estimated with Immerkær's method on flat regions. Blur is estimated by re-blurring isolated step edges, calibrated against simulated pixel-sampled edges.
2. **Alignment.** Coarse-to-fine Lucas–Kanade on luma, accurate to about 0.01 px on test scenes.
3. **Robust merge.** Kernel regression with motion rejection on low-pass differences. Moving subjects don't ghost, and per-frame aliasing (the information super-resolution needs) is kept.
4. **Super-resolution.** Iterative back-projection (Irani & Peleg) through an explicit camera model (lens blur, sub-pixel shift, pixel integration), implemented as separable parallel gathers.
5. **Restoration.** Edge-preserving guided-filter denoising, then Richardson–Lucy deconvolution with total-variation regularisation and Biggs–Andrews acceleration.
6. **Tone.** Grey-world white balance on mid-tones, percentile levels, bounded local contrast, and vibrance.

Everything is classical, deterministic signal processing that runs fully offline. No cloud and no learned model is needed. A learned model could later replace or augment individual stages, such as denoising.

## Performance

Measured on this development machine with 4 worker threads, for 8 frames at 384×256 producing a 768×512 output: about 0.6 s. On phones, work is bounded by the device-tier memory budget, input size is capped per mode, and processing runs in the background with cancellation.

## Reproduce

```sh
cmake -S native/engine -B build/engine -DPP_ENGINE_BUILD_TOOLS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/engine --target enhance_bench
# Kodak images: https://r0k.us/graphics/kodak/ (kodim01, 05, 08, 13, 15, 19, 21, 23)
python3 scripts/enhance-benchmark.py --images path/to/kodak --bench build/engine/enhance_bench --out docs/benchmarks
```
