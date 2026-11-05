"""
Real engine output for the Transform design preview.

A Kodak photo is "taken" through a simulated basic phone lens (the same
model as native/engine/tests/lens_tests.cpp: blur growing towards the
corners, red and blue imaged at a slightly different scale than green,
small-sensor grain; no darker corners, which phones correct themselves),
then run through the engine: lens analysis, Clarity alone, and Transform at
twice the detail.

  pip install numpy pillow scipy
  python3 scripts/make-transform-assets.py KODAK_DIR TRANSFORM_DEMO ENHANCE_BENCH OUT_DIR
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates

kodak, tdemo, ebench, out = (Path(a) for a in sys.argv[1:5])
out.mkdir(parents=True, exist_ok=True)


def basic_lens(src: Path, dst: Path, centre=1.2, corner=2.0, fringe=2.0, noise=0.010, seed=3) -> None:
    a = np.asarray(Image.open(src).convert("RGB")).astype(np.float64) / 255
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    h, w = lin.shape[:2]
    cy, cx = (h - 1) / 2, (w - 1) / 2
    half = 0.5 * np.hypot(w, h)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    r2 = ((xx - cx) ** 2 + (yy - cy) ** 2) / half**2
    res = np.zeros_like(lin)
    for c in range(3):
        sharp, soft = gaussian_filter(lin[..., c], centre), gaussian_filter(lin[..., c], corner)
        k = fringe / half if c == 0 else -fringe / half if c == 2 else 0.0
        sx, sy = cx + (xx - cx) / (1 + k), cy + (yy - cy) / (1 + k)
        res[..., c] = map_coordinates(sharp, [sy, sx], order=1, mode="nearest") * (1 - r2) + map_coordinates(
            soft, [sy, sx], order=1, mode="nearest"
        ) * r2
    g = np.where(res <= 0.0031308, res * 12.92, 1.055 * np.clip(res, 0, 1) ** (1 / 2.4) - 0.055)
    rng = np.random.default_rng(seed)
    g = g + noise * rng.standard_normal((h, w, 1)) + 0.4 * noise * rng.standard_normal((h, w, 3))
    Image.fromarray((np.clip(g, 0, 1) * 255 + 0.5).astype(np.uint8)).save(dst)


def run(*args: str) -> dict:
    line = subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True).stdout.strip().splitlines()[-1]
    return json.loads(line)


src = out / "t_photo.ppm"
Image.open(kodak / "kodim04.png").convert("RGB").save(src)
basic = out / "t_basic.ppm"
basic_lens(src, basic)
lens = run(tdemo, basic)["lens"]
tf = run(tdemo, basic, out / "t_tf.ppm", "--upscale", "2")
run(ebench, out / "t_clarity.ppm", basic)
(out / "t_report.json").write_text(json.dumps({"lens": lens, "transform": tf}))
print("lens", lens)
print("transform", tf)

# Same regions of each version, shown at the Transform's size (the basic
# photo and Clarity enlarged 2x, as zooming in on the phone would show them).
regions = {"face": (100, 200, 412, 512), "hat": (0, 40, 212, 252)}
for name, (x0, y0, x1, y1) in regions.items():
    for key, path, scale in [("basic", basic, 2), ("clarity", out / "t_clarity.ppm", 2), ("tf", out / "t_tf.ppm", 1)]:
        im = Image.open(path)
        f = 2 // scale
        crop = im.crop((x0 * f, y0 * f, x1 * f, y1 * f))
        if scale == 2:
            crop = crop.resize((crop.width * 2, crop.height * 2), Image.BICUBIC)
        crop.save(out / f"t_{name}_{key}.png")
for name in ["t_basic", "t_tf", "t_clarity"]:
    Image.open(out / f"{name}.ppm").save(out / f"{name}.png")
print("assets in", out)
