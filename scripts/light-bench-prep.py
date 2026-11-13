"""
Prepares real low-light and high-contrast bursts for the lighting bench
(development only, desktop).

Input: raw bursts from Google's public HDR+ Burst Photography Dataset
(hdrplusdata.org, CC-BY-SA; e.g. `gsutil -m cp -r gs://hdrplusdata/20171106_subset/bursts/<id> .`).
Each raw frame becomes what a phone camera hands the app: demosaiced,
white-balanced as shot, sRGB, 8-bit, at about 2 MP (half the sensor).

  frames_normal/frame_NNNN.ppm  auto exposure (the scene's key at 18% grey;
                                highlights clip, as in a normal phone frame)
  frames_safe/frame_NNNN.ppm    exposed for the highlights (the brightest
                                0.5% just below white)
  frames_capture/frame_NNNN.ppm what Photo Plus captures for Moonlight and
                                Bright Balance: exposure lowered to protect
                                highlights, by at most 2 stops (8-bit frames
                                cannot hold more shadow detail than that)
  info.json                     exposure, ISO, f-number, EV100, gains

  python3 scripts/light-bench-prep.py BURST_DIR OUT_DIR [--frames 8]
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image


def srgb(lin: np.ndarray) -> np.ndarray:
    lin = np.clip(lin, 0.0, 1.0)
    return np.where(lin <= 0.0031308, 12.92 * lin, 1.055 * np.power(lin, 1 / 2.4) - 0.055)


def main() -> int:
    import exifread
    import rawpy

    ap = argparse.ArgumentParser()
    ap.add_argument("burst")
    ap.add_argument("out")
    ap.add_argument("--frames", type=int, default=8)
    a = ap.parse_args()
    burst, out = Path(a.burst), Path(a.out)
    files = sorted(burst.glob("payload_N*.dng"))[: a.frames]
    with open(files[0], "rb") as fh:
        tags = exifread.process_file(fh, details=False)

    def val(*keys):
        for k in keys:
            v = tags.get(k)
            if v is not None:
                x = v.values[0]
                return float(x.num) / float(x.den) if hasattr(x, "num") else float(x)
        return None

    t = val("EXIF ExposureTime", "Image ExposureTime")
    iso = val("EXIF ISOSpeedRatings", "Image ISOSpeedRatings")
    fnum = val("EXIF FNumber", "Image FNumber") or 2.0
    ev100 = math.log2(fnum * fnum / t) - math.log2(iso / 100.0)
    lins = []
    for f in files:
        with rawpy.imread(str(f)) as raw:
            rgb = raw.postprocess(half_size=True, use_camera_wb=True, no_auto_bright=True, output_bps=16,
                                  gamma=(1, 1), output_color=rawpy.ColorSpace.sRGB, user_flip=0)
        lins.append(rgb.astype(np.float32) / 65535.0)
    ref = lins[0]
    y = 0.2126 * ref[..., 0] + 0.7152 * ref[..., 1] + 0.0722 * ref[..., 2]
    logy = np.log2(np.clip(y, 1e-6, None))
    lo, hi = np.percentile(logy, [5, 95])
    key = float(np.mean(logy[(logy >= lo) & (logy <= hi)]))
    gain_normal = 2.0 ** (math.log2(0.18) - key)
    gain_safe = 0.9 / max(float(np.percentile(y, 99.5)), 1e-6)
    gain_safe = min(gain_safe, gain_normal)
    gain_capture = gain_normal * 2.0 ** max(math.log2(gain_safe / gain_normal), -2.0)
    for name, g in (("frames_normal", gain_normal), ("frames_safe", gain_safe), ("frames_capture", gain_capture)):
        d = out / name
        d.mkdir(parents=True, exist_ok=True)
        for k, lin in enumerate(lins):
            img = (srgb(lin * g) * 255.0 + 0.5).astype(np.uint8)
            Image.fromarray(img).save(d / f"frame_{k:04d}.ppm")
    info = {"frames": len(lins), "exposure_s": t, "iso": iso, "f_number": fnum, "ev100": ev100,
            "gain_normal": gain_normal, "gain_safe": gain_safe, "gain_capture": gain_capture,
            "safe_bias_ev": math.log2(gain_safe / gain_normal), "size": list(ref.shape[1::-1])}
    (out / "info.json").write_text(json.dumps(info, indent=1))
    print(json.dumps(info))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
