#!/usr/bin/env python3
"""Cross-checks the native engine against OpenCV's reference implementations.

OpenCV is not shipped in the app (its Android SDK is >100 MB); it is used here,
on the development machine, as an independent oracle for the operations both
implement: decoding, area/bilinear/Lanczos resizing, Gaussian blur and
perspective warps. Blend modes are checked against the W3C formulas written
out in NumPy.

    python3 -m venv .venv && .venv/bin/pip install opencv-python-headless numpy
    cmake -S native/engine -B build/engine -DPP_ENGINE_BUILD_TOOLS=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build/engine --target ops_probe
    .venv/bin/python scripts/verify-engine-against-opencv.py build/engine/ops_probe [--photo some.jpg]

Exits non-zero when any comparison is outside its tolerance. Tolerances are
in 8-bit levels on the image interior (borders are handled differently by
design in some operations and are reported separately).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

import cv2
import numpy as np

RNG = np.random.default_rng(7)


def synthetic(w: int = 640, h: int = 480) -> np.ndarray:
    """Hard edges, fine texture, gradients and noise: what resampling gets wrong first."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    img = np.zeros((h, w, 3), np.float32)
    img[..., 0] = 255 * x / w
    img[..., 1] = 255 * y / h
    img[..., 2] = 128 + 100 * np.sin(x / 7.0) * np.cos(y / 11.0)
    checker = (((x // 16) + (y // 16)) % 2) * 255
    img[: h // 3, : w // 3] = checker[: h // 3, : w // 3, None]
    img = np.clip(img + 0.5, 0, 255).astype(np.uint8)  # drawing needs 8-bit images
    cv2.circle(img, (w * 2 // 3, h // 2), h // 5, (250, 30, 60), -1, lineType=cv2.LINE_AA)
    for i in range(0, w, 23):
        cv2.line(img, (i, h * 2 // 3), (i + 40, h - 1), (20, 230, 240), 1, lineType=cv2.LINE_AA)
    cv2.putText(img, "Photo Plus 4", (20, h - 30), cv2.FONT_HERSHEY_SIMPLEX, 1.6, (255, 255, 255), 3, cv2.LINE_AA)
    noisy = img + RNG.normal(0, 6, img.shape)
    return np.clip(noisy + 0.5, 0, 255).astype(np.uint8)  # RGB


class Probe:
    def __init__(self, exe: str, tmp: str):
        self.exe, self.tmp, self.n = exe, tmp, 0

    def path(self, ext: str = ".png") -> str:
        self.n += 1
        return os.path.join(self.tmp, f"f{self.n}{ext}")

    def write(self, rgb: np.ndarray, ext: str = ".png", quality: int = 92) -> str:
        p = self.path(ext)
        bgr = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
        params = [cv2.IMWRITE_JPEG_QUALITY, quality] if ext == ".jpg" else []
        assert cv2.imwrite(p, bgr, params)
        return p

    def run(self, op: str, *inputs_then_args: object, inputs: int = 1) -> np.ndarray:
        """ops_probe <op> <inputs...> <out.png> <args...>"""
        out = self.path()
        values = [str(v) for v in inputs_then_args]
        subprocess.run([self.exe, op, *values[:inputs], out, *values[inputs:]], check=True, capture_output=True)
        rgba = cv2.imread(out, cv2.IMREAD_UNCHANGED)
        return cv2.cvtColor(rgba, cv2.COLOR_BGRA2RGBA)[..., :3] if rgba.shape[2] == 4 else cv2.cvtColor(rgba, cv2.COLOR_BGR2RGB)


def stats(a: np.ndarray, b: np.ndarray, border: int = 0) -> dict:
    if border:
        a = a[border:-border, border:-border]
        b = b[border:-border, border:-border]
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    mse = float(np.mean(d.astype(np.float64) ** 2))
    return {
        "mean": float(d.mean()),
        "p99": float(np.percentile(d, 99)),
        "max": int(d.max()),
        "psnr": float("inf") if mse == 0 else 10 * np.log10(255**2 / mse),
    }


RESULTS: list[tuple[str, dict, bool]] = []


def check(name: str, s: dict, mean: float, p99: float | None = None) -> None:
    ok = s["mean"] <= mean and (p99 is None or s["p99"] <= p99)
    RESULTS.append((name, s, ok))


def w3c(cb: np.ndarray, cs: np.ndarray, mode: str) -> np.ndarray:
    """W3C Compositing and Blending Level 1, separable modes, 0..1 floats."""
    if mode == "multiply":
        return cb * cs
    if mode == "screen":
        return cb + cs - cb * cs
    if mode == "hardlight":
        return np.where(cs <= 0.5, cb * 2 * cs, w3c(cb, 2 * cs - 1, "screen"))
    if mode == "overlay":
        return w3c(cs, cb, "hardlight")
    if mode == "softlight":
        d = np.where(cb <= 0.25, ((16 * cb - 12) * cb + 4) * cb, np.sqrt(cb))
        return np.where(cs <= 0.5, cb - (1 - 2 * cs) * cb * (1 - cb), cb + (2 * cs - 1) * (d - cb))
    if mode == "darken":
        return np.minimum(cb, cs)
    if mode == "lighten":
        return np.maximum(cb, cs)
    if mode == "difference":
        return np.abs(cb - cs)
    if mode == "add":
        return np.minimum(1, cb + cs)
    raise ValueError(mode)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("probe", help="path to the ops_probe tool")
    ap.add_argument("--photo", action="append", default=[], help="extra real photo(s) to test on")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        probe = Probe(args.probe, tmp)
        images = {"synthetic": synthetic()}
        for i, p in enumerate(args.photo):
            bgr = cv2.imread(p, cv2.IMREAD_COLOR)
            if bgr is None:
                print(f"cannot read {p}", file=sys.stderr)
                return 2
            scale = min(1.0, 1600 / max(bgr.shape[:2]))  # keep the run quick
            bgr = cv2.resize(bgr, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
            images[f"photo{i + 1}"] = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)

        for label, img in images.items():
            h, w = img.shape[:2]
            png = probe.write(img)

            # Decoding: PNG is lossless (must be identical); JPEG decoders may
            # differ in IDCT and chroma upsampling by a level or so.
            check(f"{label}: decode PNG", stats(probe.run("decode", png), img), 0.0)
            jpg = probe.write(img, ".jpg")
            ref = cv2.cvtColor(cv2.imread(jpg, cv2.IMREAD_COLOR), cv2.COLOR_BGR2RGB)
            check(f"{label}: decode JPEG", stats(probe.run("decode", jpg), ref), 1.0, 4)

            # Area shrinking (exact and fractional factors).
            for f in (2, 3, 2.5):
                tw, th = round(w / f), round(h / f)
                ref = cv2.resize(img, (tw, th), interpolation=cv2.INTER_AREA)
                check(f"{label}: area shrink /{f}", stats(probe.run("resize", png, tw, th, 2), ref), 0.6, 3)
            # Auto picks area when shrinking.
            tw, th = w // 3, h // 3
            ref = cv2.resize(img, (tw, th), interpolation=cv2.INTER_AREA)
            check(f"{label}: auto shrink = area", stats(probe.run("resize", png, tw, th, 0), ref), 0.6, 3)
            # Bilinear enlargement (both use pixel-centre alignment).
            tw, th = int(w * 1.7), int(h * 1.7)
            ref = cv2.resize(img, (tw, th), interpolation=cv2.INTER_LINEAR)
            check(f"{label}: bilinear x1.7", stats(probe.run("resize", png, tw, th, 1), ref, 2), 0.6, 3)
            # Lanczos-3 (engine) vs Lanczos-4 (OpenCV): close, not identical kernels.
            ref = cv2.resize(img, (tw, th), interpolation=cv2.INTER_LANCZOS4)
            s = stats(probe.run("resize", png, tw, th, 3), ref, 4)
            check(f"{label}: Lanczos x1.7 (3 vs 4 lobes)", s, 1.5)

            # Gaussian blur, replicated borders. Large sigmas run on a reduced
            # copy in the engine (cost stays flat): looser, still close.
            for sigma, tol in ((1.5, 0.6), (4.0, 0.6), (12.0, 1.5), (30.0, 1.5)):
                ref = cv2.GaussianBlur(img, (0, 0), sigma, borderType=cv2.BORDER_REPLICATE)
                s = stats(probe.run("blur", png, sigma), ref, int(3 * sigma))
                check(f"{label}: blur sigma {sigma}", s, tol)

            # Perspective warp. The engine's quad is in continuous coordinates
            # (pixel centres at +0.5); OpenCV's are at integers.
            quad = np.float32([[w * 0.12, h * 0.08], [w * 0.93, h * 0.15], [w * 0.85, h * 0.95], [w * 0.05, h * 0.88]])
            ow, oh = 560, 420
            rect = np.float32([[0, 0], [ow, 0], [ow, oh], [0, oh]])
            hmat = cv2.getPerspectiveTransform(rect, quad)
            t = lambda d: np.array([[1, 0, d], [0, 1, d], [0, 0, 1]], np.float64)
            m = t(-0.5) @ hmat @ t(0.5)
            ref = cv2.warpPerspective(img, m, (ow, oh), flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP,
                                      borderMode=cv2.BORDER_REPLICATE)
            out = probe.run("warp", png, ow, oh, *quad.reshape(-1).tolist())
            check(f"{label}: perspective warp", stats(out, ref, 1), 0.6, 3)

        # Blend modes on two opaque images vs the W3C formulas.
        base = images["synthetic"]
        layer = np.ascontiguousarray(base[::-1, ::-1])
        bp, lp = probe.write(base), probe.write(layer)
        cb, cs = base / 255.0, layer / 255.0
        for mode, mid in (("multiply", 1), ("screen", 2), ("overlay", 3), ("softlight", 4), ("hardlight", 5),
                          ("darken", 6), ("lighten", 7), ("difference", 8), ("add", 9)):
            for opacity in (1.0, 0.6):
                ref = cb * (1 - opacity) + w3c(cb, cs, mode) * opacity
                ref = np.clip(ref * 255 + 0.5, 0, 255).astype(np.uint8)
                out = probe.run("blend", bp, lp, mid, opacity, inputs=2)
                check(f"blend {mode} @{opacity}", stats(out, ref), 0.6, 1)

    width = max(len(n) for n, _, _ in RESULTS)
    failed = 0
    print(f"{'check':<{width}}  {'mean':>6} {'p99':>5} {'max':>4} {'PSNR':>7}")
    for name, s, ok in RESULTS:
        failed += not ok
        psnr = "inf" if s["psnr"] == float("inf") else f"{s['psnr']:.1f}"
        print(f"{name:<{width}}  {s['mean']:6.3f} {s['p99']:5.1f} {s['max']:4d} {psnr:>7}  {'ok' if ok else 'FAIL'}")
    print(f"\n{len(RESULTS) - failed}/{len(RESULTS)} within tolerance (OpenCV {cv2.__version__})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
