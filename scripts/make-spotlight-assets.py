"""
Real engine output for the Spotlight / Glow / Vibe Match design preview.

On the phone, subject and face masks come from ML Kit. Here the same kind of
masks come from Google's MediaPipe selfie segmenter and face landmarker
(models downloaded separately), then the Photo Plus engine runs on them via
the portrait_demo tool. The Vibe Match reference is a Kodak photo graded with
a teal-and-orange preset.

  python3 scripts/make-spotlight-assets.py KODAK_DIR PORTRAIT_DEMO MASK_DIR OUT_DIR
    MASK_DIR holds ml04_* / ml18_* masks (see docs/PHASE2_REPORT.md) and
    depth18.pgm from scripts/estimate-depth.py (the app's depth model)
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

kodak, demo, masks, out = (Path(a) for a in sys.argv[1:5])
out.mkdir(parents=True, exist_ok=True)


def ppm(name: str) -> Path:
    p = out / f"{name}.ppm"
    Image.open(kodak / f"{name}.png").convert("RGB").save(p)
    return p


def run(src: Path, dst: Path, key: str, *extra: str) -> dict:
    args = [str(demo), str(src), str(dst), "--subject-mask", str(masks / f"ml{key}_subject.pgm"),
            "--faces-mask", str(masks / f"ml{key}_faces.pgm"), "--protect-mask", str(masks / f"ml{key}_protect.pgm"), *extra]
    line = subprocess.run(args, check=True, capture_output=True, text=True).stdout.strip()
    print(dst.name, line)
    return json.loads(line)


k18, k04, k05 = ppm("kodim18"), ppm("kodim04"), ppm("kodim05")
depth = ["--depth-mask", str(masks / "depth18.pgm")]
run(k18, out / "spot18.ppm", "18", *depth)                       # Spotlight, default (Medium blur, Smooth glow)
run(k18, out / "ground18.ppm", "18", "--retouch", "0", "--pop", "0", "--brighten", "0")  # no depth: estimated ground
glow = run(k18, out / "glow18.ppm", "18", "--retouch", "1.0", *depth)  # Glow: Flawless, on real blemishes
(out / "glow18.json").write_text(json.dumps(glow))

# Glow close-up: her face is ~55 px across in this photo, so the screen
# shows it enlarged (the same crop of the original and of the result).
f = np.asarray(Image.open(masks / "ml18_faces.pgm")) > 127
scale = Image.open(k18).width / f.shape[1]
ys, xs = np.nonzero(f)
cx, cy = (xs.min() + xs.max()) / 2 * scale, (ys.min() + ys.max()) / 2 * scale
half_w = (xs.max() - xs.min()) * scale * 0.95
box = (int(cx - half_w), int(cy - half_w * 1.25), int(cx + half_w), int(cy + half_w * 1.6))
for name, src in [("face18", k18), ("glowface18", out / "glow18.ppm")]:
    im = Image.open(src).crop(box)
    im.resize((im.width * 4, im.height * 4), Image.LANCZOS).save(out / f"{name}.png")

# Depth explainer: the depth map (near = bright) and the ground at her feet.
d = Image.open(masks / "depth18.pgm").resize(Image.open(k18).size, Image.BILINEAR)
d.convert("RGB").save(out / "depth18.png")
feet = (150, 560, 512, 768)
for name, src in [("feet18", k18), ("spotfeet18", out / "spot18.ppm")]:
    im = Image.open(src).crop(feet)
    im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(out / f"{name}.png")

# Teal-and-orange reference look (lifted blacks, softer colour, vignette).
a = np.asarray(Image.open(k05), dtype=np.float64) / 255.0
l = a @ np.array([0.299, 0.587, 0.114])
g = l[..., None] + 0.8 * (a - l[..., None])
sh = np.clip(1 - 2 * l, 0, 1)[..., None]
hi = np.clip(2 * l - 1, 0, 1)[..., None]
g = g + hi * np.array([0.07, 0.02, -0.05]) + sh * np.array([-0.04, 0.01, 0.06])
g = 0.07 + 0.9 * np.clip(g, 0, 1) ** 0.95
h, w = l.shape
yy, xx = np.mgrid[0:h, 0:w]
v = 1 - 0.28 * np.clip(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2, 0, 2) / 2
ref = (np.clip(g * v[..., None], 0, 1) * 255).astype(np.uint8)
Image.fromarray(ref).save(out / "look_ref.ppm")
subprocess.run([str(demo), str(k18), str(out / "vibe18.ppm"), "--look", str(out / "look_ref.ppm"), "--strength", "0.9",
                "--blur", "0", "--retouch", "0", "--brighten", "0", "--pop", "0", "--subject-mask",
                str(masks / "ml18_subject.pgm")], check=True, capture_output=True)
for name in ["spot18", "ground18", "glow18", "look_ref", "vibe18", "kodim18"]:
    Image.open(out / f"{name}.ppm").save(out / f"{name}.png")
print("assets in", out)
