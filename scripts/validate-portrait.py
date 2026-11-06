"""
Validates the portrait tools on real faces (development only, desktop).

On the phone, face landmarks come from ML Kit face contours and the subject
mask from ML Kit segmentation. Here Google's MediaPipe face landmarker and
selfie segmenter stand in for them: the 478-point face mesh is mapped onto
the same 131-point layout the app sends to the engine (PP_FACE_* in
pp_engine.h), and the face and feature masks are drawn the way the app
draws them. The engine then runs through the portrait_demo tool.

For every photo it measures what decides whether a retouch looks natural:
  - skin hue and brightness change on the cheeks (the person's own tone must
    be kept; reported in degrees of hue and levels of luma),
  - fine texture kept on the cheeks (real skin, not plastic),
  - change outside the face and subject (must be ~0 where nothing was asked).
Results are grouped by measured skin tone (Individual Typology Angle, the
dermatology scale from very light to dark), not by any demographic label.

  pip install numpy pillow mediapipe pyarrow
  python3 scripts/validate-portrait.py PORTRAIT_DEMO LANDMARKER.task SEGMENTER.tflite OUT_DIR \\
      [--fairface VALIDATION.parquet --count 48] [PHOTO ...]

Writes OUT_DIR/results.json, a per-photo table, and contact sheets.
"""
from __future__ import annotations

import argparse
import io
import json
import math
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# MediaPipe face-mesh indices for each PP_FACE_* contour (same order and
# counts as ML Kit's contours).
MESH = {
    "oval": [10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377,
             152, 148, 176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109],
    "left_brow_top": [300, 293, 334, 296, 336],
    "left_brow_bottom": [276, 283, 282, 295, 285],
    "right_brow_top": [70, 63, 105, 66, 107],
    "right_brow_bottom": [46, 53, 52, 65, 55],
    "left_eye": [263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466],
    "right_eye": [33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246],
    "upper_lip_top": [61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291],
    "upper_lip_bottom": [191, 80, 81, 82, 13, 312, 311, 310, 415],
    "lower_lip_top": [95, 88, 178, 87, 14, 317, 402, 318, 324],
    "lower_lip_bottom": [146, 91, 181, 84, 17, 314, 405, 321, 375],
    "nose_bridge": [168, 4],
    "nose_bottom": [64, 2, 294],
}
ORDER = ["oval", "left_brow_top", "left_brow_bottom", "right_brow_top", "right_brow_bottom", "left_eye",
         "right_eye", "upper_lip_top", "upper_lip_bottom", "lower_lip_top", "lower_lip_bottom", "nose_bridge",
         "nose_bottom"]
assert sum(len(MESH[k]) for k in ORDER) == 131

# The settings people choose: the free presets and the Premium tools.
CONFIGS = {
    "glow_natural": dict(retouch=0.5, brighten=0.3),
    "glow_flawless": dict(retouch=1.0, brighten=0.5),
    "advanced": dict(retouch=0.0, smooth=0.6, blemish=1.0, even=0.6, brighten=0.3,
                     eyes=0.6, teeth=0.6, lips=0.4, under_eye=0.7, shine=0.7, face_light=0.6),
    "studio": dict(retouch=0.5, brighten=0.3, studio=0.8, light_angle=-0.6, face_light=0.4),
    "slim": dict(retouch=0.0, brighten=0.0, slim=0.6),
}
FLAGS = {"under_eye": "--under-eye", "face_light": "--face-light", "light_angle": "--light-angle",
         "bg_light": "--bg-light", "bg_color": "--bg-color"}


def ycc(rgb: np.ndarray) -> np.ndarray:
    r, g, b = (rgb[..., i].astype(np.float32) / 255.0 for i in range(3))
    return np.stack([0.299 * r + 0.587 * g + 0.114 * b, -0.168736 * r - 0.331264 * g + 0.5 * b,
                     0.5 * r - 0.418688 * g - 0.081312 * b], -1)


def lab(rgb: np.ndarray) -> np.ndarray:
    """CIE L*a*b* (D65) of the median colour of a set of sRGB pixels."""
    c = np.median(rgb.reshape(-1, 3).astype(np.float64) / 255.0, 0)
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    x, y, z = m @ lin / np.array([0.95047, 1.0, 1.08883])
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    return np.array([116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))])


def lab_ita(rgb: np.ndarray) -> float:
    """Individual Typology Angle of the median skin colour (degrees)."""
    L, _, b = lab(rgb)
    return math.degrees(math.atan2(L - 50, b))


def ita_group(ita: float) -> str:
    for lim, name in ((55, "very light"), (41, "light"), (28, "intermediate"), (10, "tan"), (-30, "brown")):
        if ita > lim:
            return name
    return "dark"


def detect(photo: Image.Image, landmarker, segmenter):
    import mediapipe as mp
    arr = np.asarray(photo.convert("RGB"))
    image = mp.Image(image_format=mp.ImageFormat.SRGB, data=arr)
    faces = landmarker.detect(image).face_landmarks
    seg = np.squeeze(segmenter.segment(image).category_mask.numpy_view())
    subject = (seg > 0).astype(np.uint8) * 255  # anything but background
    return faces, subject


def landmarks_of(face) -> list[float]:
    out = []
    for k in ORDER:
        for i in MESH[k]:
            out += [face[i].x, face[i].y]
    return out


def draw_masks(size, faces):
    """Face areas and protected features, as the app draws them."""
    w, h = size
    fm, pm = Image.new("L", size, 0), Image.new("L", size, 0)
    fd, pd = ImageDraw.Draw(fm), ImageDraw.Draw(pm)
    for f in faces:
        pt = lambda i: (f[i].x * w, f[i].y * h)
        fd.polygon([pt(i) for i in MESH["oval"]], fill=255)
        for k in ("left_eye", "right_eye"):
            pd.polygon([pt(i) for i in MESH[k]], fill=255)
        for a, b in (("left_brow_top", "left_brow_bottom"), ("right_brow_top", "right_brow_bottom"),
                     ("upper_lip_top", "lower_lip_bottom")):
            pd.polygon([pt(i) for i in MESH[a]] + [pt(i) for i in reversed(MESH[b])], fill=255)
    pm = pm.filter(ImageFilter.MaxFilter(5))
    return fm, pm


def cheeks(faces, size) -> np.ndarray:
    """Cheek areas (for measurements): beside the nose, below the eyes."""
    w, h = size
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    for f in faces:
        for a, b in ((50, 205), (280, 425)):  # mesh points on each cheek
            cx, cy = (f[a].x + f[b].x) / 2 * w, (f[a].y + f[b].y) / 2 * h
            r = abs(f[454].x - f[234].x) * w * 0.06
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    return np.asarray(m) > 127


def fine_texture(y: np.ndarray, region: np.ndarray) -> float:
    lap = np.abs(y[1:-1, 1:-1] - 0.25 * (y[:-2, 1:-1] + y[2:, 1:-1] + y[1:-1, :-2] + y[1:-1, 2:]))
    r = region[1:-1, 1:-1]
    return float(lap[r].mean()) if r.any() else 0.0


def run(demo: Path, src: Path, dst: Path, masks: dict, lm_file: Path | None, cfg: dict) -> dict:
    args = [str(demo), str(src), str(dst), "--subject-mask", str(masks["subject"]), "--faces-mask", str(masks["faces"]),
            "--protect-mask", str(masks["protect"]), "--blur", "0", "--pop", "0"]
    for k, v in cfg.items():
        args += [FLAGS.get(k, "--" + k.replace("_", "-")), str(v)]
    if lm_file is not None:
        args += ["--landmarks", str(lm_file)]
    out = subprocess.run(args, check=True, capture_output=True, text=True).stdout.strip()
    return json.loads(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("demo", type=Path)
    ap.add_argument("landmarker", type=Path)
    ap.add_argument("segmenter", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("photos", nargs="*", type=Path)
    ap.add_argument("--fairface", type=Path)
    ap.add_argument("--count", type=int, default=48)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)

    photos: list[tuple[str, Image.Image]] = [(p.stem, Image.open(p).convert("RGB")) for p in a.photos]
    if a.fairface:
        import pyarrow.parquet as pq
        t = pq.read_table(a.fairface).to_pydict()
        rng = np.random.default_rng(5)
        # Adults, balanced across the dataset's groups (labels used only to spread the sample).
        by_group: dict[int, list[int]] = {}
        for i, (age, race) in enumerate(zip(t["age"], t["race"])):
            if 3 <= age <= 6:
                by_group.setdefault(race, []).append(i)
        per = max(1, a.count // max(1, len(by_group)))
        for g, idx in sorted(by_group.items()):
            for i in rng.choice(idx, size=min(per, len(idx)), replace=False):
                img = Image.open(io.BytesIO(t["image"][i]["bytes"])).convert("RGB")
                photos.append((f"ff{i}", img))

    import mediapipe as mp
    from mediapipe.tasks.python import BaseOptions, vision
    landmarker = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=str(a.landmarker)), num_faces=4))
    segmenter = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=str(a.segmenter)), output_category_mask=True))
    results = []
    try:
        for name, photo in photos:
            faces, subject = detect(photo, landmarker, segmenter)
            if not faces:
                print(f"{name}: no face found, skipped")
                continue
            d = a.out / name
            d.mkdir(exist_ok=True)
            src = d / "in.ppm"
            photo.save(src)
            Image.fromarray(subject).save(d / "subject.pgm")
            fm, pm = draw_masks(photo.size, faces)
            fm.save(d / "faces.pgm")
            pm.save(d / "protect.pgm")
            lm_file = d / "landmarks.txt"
            lm_file.write_text("\n".join(",".join(f"{v:.5f}" for v in landmarks_of(f)) for f in faces) + "\n")
            masks = {"subject": d / "subject.pgm", "faces": d / "faces.pgm", "protect": d / "protect.pgm"}
            before = np.asarray(photo)
            ck = cheeks(faces, photo.size)
            if ck.sum() < 20:
                continue
            ita = lab_ita(before[ck])
            y0 = ycc(before)
            row = {"photo": name, "size": photo.size, "faces": len(faces), "ita": round(ita, 1), "tone": ita_group(ita),
                   "faceWidth": round(abs(faces[0][454].x - faces[0][234].x) * photo.size[0])}
            outside = np.asarray(Image.open(d / "subject.pgm").filter(ImageFilter.MaxFilter(9))) < 10
            for cname, cfg in CONFIGS.items():
                dst = d / f"{cname}.ppm"
                rep = run(a.demo, src, dst, masks, lm_file, cfg)
                after = np.asarray(Image.open(dst).convert("RGB"))
                y1 = ycc(after)
                hue0 = math.degrees(math.atan2(np.median(y0[..., 2][ck]), np.median(y0[..., 1][ck])))
                hue1 = math.degrees(math.atan2(np.median(y1[..., 2][ck]), np.median(y1[..., 1][ck])))
                t0, t1 = fine_texture(y0[..., 0], ck), fine_texture(y1[..., 0], ck)
                lab0, lab1 = lab(before[ck]), lab(after[ck])
                row[cname] = {
                    # Colour change of the skin, separately for its colour
                    # (a*, b*: the tone itself) and its lightness.
                    "toneDeltaE": round(float(np.hypot(*(lab1[1:] - lab0[1:]))), 2),
                    "lightnessChange": round(float(lab1[0] - lab0[0]), 2),
                    "hueShiftDeg": round(abs(hue1 - hue0), 2),
                    "lumaChange": round(float(255 * (np.median(y1[..., 0][ck]) - np.median(y0[..., 0][ck]))), 2),
                    "textureKept": round(t1 / t0, 3) if t0 > 0 else None,
                    "outsideChange": round(float(np.abs(after.astype(int) - before.astype(int))[outside].mean()), 3)
                    if outside.any() else 0.0,
                    "ms": rep["ms"],
                    "lightGain": rep.get("lightGain"),
                }
            results.append(row)
            print(json.dumps(row))
    finally:
        landmarker.close()
        segmenter.close()
    (a.out / "results.json").write_text(json.dumps(results, indent=1))
    # Summary per skin-tone group.
    summary = {}
    for r in results:
        g = summary.setdefault(r["tone"], {"photos": 0})
        g["photos"] += 1
        for c in CONFIGS:
            for k in ("toneDeltaE", "lightnessChange", "hueShiftDeg", "lumaChange", "textureKept", "outsideChange"):
                v = r[c][k]
                if v is not None:
                    g.setdefault(c, {}).setdefault(k, []).append(v)
    for g in summary.values():
        for c in CONFIGS:
            if c in g:
                g[c] = {k: {"median": round(float(np.median(v)), 3), "worst": round(float(np.max(np.abs(v))), 3)}
                        for k, v in g[c].items()}
    (a.out / "summary.json").write_text(json.dumps(summary, indent=1))
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
