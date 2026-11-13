"""
Turns a well-lit photo into a realistic dark-room burst, for testing the
low-light pipeline on a familiar picture (development only, desktop).

The photo is taken back to linear light, given `--stops` less light, and
each frame gets independent sensor noise (photon "shot" noise, which grows
with the signal, plus read noise; the model used for raw denoising, e.g.
Brooks et al., "Unprocessing Images for Learned Raw Denoising", CVPR 2019)
and a small random hand-shake shift; then the camera's gain brings it back
up to a normal brightness, as a phone's auto exposure would, and it is
stored as an 8-bit frame.

  python3 scripts/light-darkroom-sim.py PHOTO OUT_DIR [--frames 8] [--stops 4] [--size 2048]

Writes OUT_DIR/frame_NNNN.ppm.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps


def to_linear(v: np.ndarray) -> np.ndarray:
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def to_srgb(v: np.ndarray) -> np.ndarray:
    v = np.clip(v, 0.0, 1.0)
    return np.where(v <= 0.0031308, 12.92 * v, 1.055 * np.power(v, 1 / 2.4) - 0.055)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("photo")
    ap.add_argument("out")
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--stops", type=float, default=4.0)
    ap.add_argument("--size", type=int, default=2048)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    im = ImageOps.exif_transpose(Image.open(a.photo)).convert("RGB")
    im.thumbnail((a.size, a.size), Image.LANCZOS)
    lin = to_linear(np.asarray(im, np.float64) / 255.0)
    rng = np.random.default_rng(a.seed)
    gain = 2.0 ** a.stops
    shot, read = 0.0016, 4e-6  # variance = shot·signal + read, on the dimmed signal
    h, w, _ = lin.shape
    for k in range(a.frames):
        dx, dy = rng.integers(-3, 4, size=2)
        shifted = np.roll(np.roll(lin, int(dy), axis=0), int(dx), axis=1)
        dark = shifted / gain
        noisy = dark + rng.normal(0.0, 1.0, dark.shape) * np.sqrt(shot * dark + read)
        frame = to_srgb(noisy * gain)
        Image.fromarray((frame * 255.0 + 0.5).astype(np.uint8)).save(out / f"frame_{k:04d}.ppm")
    print(f"{a.frames} frames, {w}x{h}, {a.stops} stops darker")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
