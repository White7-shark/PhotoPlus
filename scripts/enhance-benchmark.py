#!/usr/bin/env python3
"""
Photo Plus enhancement benchmark: "a weak camera can still take great photos".

For each ground-truth photo it simulates a cheap phone camera, runs the real
C++ engine (native/engine, via the enhance_bench tool), and scores results
against the original with PSNR and SSIM.

Simulated camera (per frame of an 8-frame handheld burst):
  * half the linear resolution of the original (1/4 of the megapixels)
  * soft lens: Gaussian blur, sigma = 1.0 px at full resolution
  * hand-shake: random sub-pixel shifts of up to +/-1.5 px
  * sensor noise: signal-dependent (shot) + read noise
  * 8-bit quantisation and JPEG compression (quality 88), like a phone ISP

Compared at the original resolution:
  * bicubic   - what an ordinary app gives: one frame, upscaled
  * enhance-1 - Photo Plus single-frame restoration (2x)
  * enhance-8 - Photo Plus 8-frame burst super-resolution (2x)

Requires numpy + Pillow. Usage:
  python3 scripts/enhance-benchmark.py --images DIR --bench PATH/enhance_bench --out docs/benchmarks
"""
from __future__ import annotations

import argparse
import io
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

RNG = np.random.default_rng(2026)
FRAMES = 8
SCALE = 2
LENS_SIGMA = 1.0
MAX_SHIFT = 1.5
SHOT = 0.0006  # variance per unit signal
READ = 0.012   # read-noise std-dev
JPEG_Q = 88


def gaussian_kernel(sigma: float) -> np.ndarray:
    r = max(1, int(np.ceil(3 * sigma)))
    x = np.arange(-r, r + 1, dtype=np.float64)
    k = np.exp(-(x * x) / (2 * sigma * sigma))
    return k / k.sum()


def blur(img: np.ndarray, sigma: float) -> np.ndarray:
    k = gaussian_kernel(sigma)
    r = len(k) // 2
    out = np.pad(img, ((r, r), (0, 0), (0, 0)), mode="edge")
    out = sum(k[i] * out[i : i + img.shape[0]] for i in range(len(k)))
    out = np.pad(out, ((0, 0), (r, r), (0, 0)), mode="edge")
    return sum(k[i] * out[:, i : i + img.shape[1]] for i in range(len(k)))


def shift(img: np.ndarray, dx: float, dy: float) -> np.ndarray:
    """out(x) = img(x + d), bilinear with edge clamping."""
    h, w = img.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    x = np.clip(xs + dx, 0, w - 1)
    y = np.clip(ys + dy, 0, h - 1)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
    ax, ay = (x - x0)[..., None], (y - y0)[..., None]
    top = img[y0, x0] * (1 - ax) + img[y0, x1] * ax
    bot = img[y1, x0] * (1 - ax) + img[y1, x1] * ax
    return top * (1 - ay) + bot * ay


def capture(truth: np.ndarray, dx: float, dy: float) -> np.ndarray:
    img = blur(shift(truth, dx, dy), LENS_SIGMA)
    h, w = img.shape[0] // SCALE * SCALE, img.shape[1] // SCALE * SCALE
    img = img[:h, :w].reshape(h // SCALE, SCALE, w // SCALE, SCALE, 3).mean(axis=(1, 3))
    noise = RNG.normal(size=img.shape) * np.sqrt(SHOT * np.clip(img, 0, 1) + READ * READ)
    img = np.clip(img + noise, 0, 1)
    pil = Image.fromarray((img * 255 + 0.5).astype(np.uint8))
    buf = io.BytesIO()
    pil.save(buf, format="JPEG", quality=JPEG_Q)
    return np.asarray(Image.open(buf).convert("RGB"), dtype=np.float64) / 255.0


def psnr(a: np.ndarray, b: np.ndarray, border: int = 16) -> float:
    a, b = a[border:-border, border:-border], b[border:-border, border:-border]
    mse = np.mean((a - b) ** 2)
    return float(10 * np.log10(1.0 / max(mse, 1e-12)))


def ssim(a: np.ndarray, b: np.ndarray, border: int = 16) -> float:
    def luma(x: np.ndarray) -> np.ndarray:
        return (0.299 * x[..., 0] + 0.587 * x[..., 1] + 0.114 * x[..., 2])[..., None]

    x, y = luma(a), luma(b)
    c1, c2 = 0.01**2, 0.03**2
    mx, my = blur(x, 1.5), blur(y, 1.5)
    sxx = blur(x * x, 1.5) - mx * mx
    syy = blur(y * y, 1.5) - my * my
    sxy = blur(x * y, 1.5) - mx * my
    m = ((2 * mx * my + c1) * (2 * sxy + c2)) / ((mx * mx + my * my + c1) * (sxx + syy + c2))
    return float(m[border:-border, border:-border].mean())


def to_pil(img: np.ndarray) -> Image.Image:
    return Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))


def write_ppm(path: Path, img: np.ndarray) -> None:
    to_pil(img).save(path, format="PPM")


def read_ppm(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0


def run_engine(bench: Path, frames: list[Path], out: Path, extra: list[str]) -> dict:
    cmd = [str(bench), "--upscale", str(SCALE), *extra, str(out), *map(str, frames)]
    res = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return json.loads(res.stdout.strip().splitlines()[-1])


def label(img: Image.Image, text: str, sub: str) -> Image.Image:
    canvas = Image.new("RGB", (img.width, img.height + 54), (10, 9, 17))
    canvas.paste(img, (0, 54))
    d = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("DejaVuSans-Bold.ttf", 20)
        small = ImageFont.truetype("DejaVuSans.ttf", 15)
    except OSError:
        font = small = ImageFont.load_default()
    d.text((12, 6), text, fill=(247, 245, 251), font=font)
    d.text((12, 31), sub, fill=(255, 176, 31), font=small)
    return canvas


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--images", required=True, type=Path)
    ap.add_argument("--bench", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--visuals", default="kodim23,kodim05,kodim13")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    visuals = set(args.visuals.split(","))
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        for path in sorted(args.images.glob("*.png")):
            name = path.stem
            truth = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
            h, w = truth.shape[0] // SCALE * SCALE, truth.shape[1] // SCALE * SCALE
            truth = truth[:h, :w]
            shifts = [(0.0, 0.0)] + [tuple(RNG.uniform(-MAX_SHIFT, MAX_SHIFT, 2)) for _ in range(FRAMES - 1)]
            frames = []
            for i, (dx, dy) in enumerate(shifts):
                f = t / f"{name}_{i}.ppm"
                write_ppm(f, capture(truth, dx, dy))
                frames.append(f)
            lr0 = read_ppm(frames[0])
            bicubic = np.asarray(to_pil(lr0).resize((w, h), Image.BICUBIC), dtype=np.float64) / 255.0
            single_out, burst_out, look_out = t / "single.ppm", t / "burst.ppm", t / "look.ppm"
            r1 = run_engine(args.bench, frames[:1], single_out, ["--restore-only"])
            r8 = run_engine(args.bench, frames, burst_out, ["--restore-only"])
            single, burst = read_ppm(single_out), read_ppm(burst_out)
            row = {
                "image": name,
                "bicubic": {"psnr": psnr(bicubic, truth), "ssim": ssim(bicubic, truth)},
                "single": {"psnr": psnr(single, truth), "ssim": ssim(single, truth), "ms": r1["ms"]},
                "burst": {"psnr": psnr(burst, truth), "ssim": ssim(burst, truth), "ms": r8["ms"],
                          "framesUsed": r8["framesUsed"]},
                "estimated": {"noise": r1["noiseBefore"], "blurPx": r1["blurBefore"]},
            }
            rows.append(row)
            print(f"{name}: bicubic {row['bicubic']['psnr']:.2f} dB / SSIM {row['bicubic']['ssim']:.3f} | "
                  f"single {row['single']['psnr']:.2f} / {row['single']['ssim']:.3f} | "
                  f"burst {row['burst']['psnr']:.2f} / {row['burst']['ssim']:.3f} "
                  f"({r8['framesUsed']}/{FRAMES} frames, {r8['ms']:.0f} ms)")

            if name in visuals:
                # Full "Enhance" look (restoration + auto tone), for the demo.
                run_engine(args.bench, frames, look_out, [])
                look = read_ppm(look_out)
                # 1:1 crop of a detailed region at output resolution.
                cw, ch = min(360, w), min(300, h)
                x0, y0 = (w - cw) // 2, (h - ch) // 2
                crop = lambda im: to_pil(im[y0 : y0 + ch, x0 : x0 + cw])  # noqa: E731
                phone = to_pil(lr0).resize((w, h), Image.NEAREST)
                panels = [
                    label(crop(np.asarray(phone, dtype=np.float64) / 255.0), "Weak phone camera",
                          f"{lr0.shape[1]}x{lr0.shape[0]}, noisy, soft lens"),
                    label(crop(bicubic), "Ordinary upscale",
                          f"bicubic  {row['bicubic']['psnr']:.1f} dB"),
                    label(crop(burst), "Photo Plus burst SR",
                          f"8 frames  {row['burst']['psnr']:.1f} dB  SSIM {row['burst']['ssim']:.2f}"),
                    label(crop(look), "Photo Plus Enhance",
                          "burst SR + auto tone"),
                    label(crop(truth), "Original", f"{w}x{h} reference"),
                ]
                gap = 10
                sheet = Image.new("RGB", (sum(p.width for p in panels) + gap * (len(panels) + 1),
                                          panels[0].height + 2 * gap), (24, 22, 32))
                x = gap
                for p in panels:
                    sheet.paste(p, (x, gap))
                    x += p.width + gap
                sheet.save(args.out / f"compare-{name}.png", optimize=True)

    def mean(key: str, metric: str) -> float:
        return float(np.mean([r[key][metric] for r in rows]))

    summary = {
        "simulation": {"frames": FRAMES, "scale": SCALE, "lensSigma": LENS_SIGMA, "maxShift": MAX_SHIFT,
                       "shotVariance": SHOT, "readNoise": READ, "jpegQuality": JPEG_Q},
        "mean": {k: {"psnr": mean(k, "psnr"), "ssim": mean(k, "ssim")} for k in ("bicubic", "single", "burst")},
        "images": rows,
    }
    (args.out / "enhancement-results.json").write_text(json.dumps(summary, indent=2))
    m = summary["mean"]
    print(f"\nMEAN  bicubic {m['bicubic']['psnr']:.2f} dB / {m['bicubic']['ssim']:.3f} | "
          f"single {m['single']['psnr']:.2f} / {m['single']['ssim']:.3f} | "
          f"burst {m['burst']['psnr']:.2f} / {m['burst']['ssim']:.3f}")


if __name__ == "__main__":
    main()
