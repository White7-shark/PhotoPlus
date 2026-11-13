"""
Runs the 13 Skin Tone shades on real photos (development only, desktop) and
measures what decides whether each one works:

  - reaches the skin: on face skin and body skin (neck, arms, hands), the
    skin's colour changes by the shade's gains (measured in linear light)
    while the person's shading and texture stay;
  - nothing else changes: hair, clothes and the background keep their colour
    (mean CIELAB difference), as do the protected features;
  - distinct: every shade gives a visibly different skin (closest pair);
  - no clipping: highlights never newly clip.

MediaPipe stands in for ML Kit: the face mesh gives the face area and the
protected features (as the app draws them, validate-portrait.py), the
multiclass selfie segmenter gives the subject and, for measuring only, which
pixels are hair, face skin, body skin and clothes.

  python3 scripts/skintone-demo.py SKINTONE_DEMO LANDMARKER.task SEGMENTER.tflite OUT_DIR \\
      [--fairface VALIDATION.parquet --count 16] [--strength 1] PHOTO ...

Writes OUT_DIR/results.json and a labelled sheet per photo (original + 13).
"""
from __future__ import annotations

import argparse
import importlib.util
import io
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("validate_portrait", here / "validate-portrait.py")
vp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vp)

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
HAIR, BODY, FACE, CLOTHES = 1, 2, 3, 4  # selfie_multiclass categories


def srgb_to_lab(rgb: np.ndarray) -> np.ndarray:
    c = rgb.astype(np.float64) / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def ita(lab: np.ndarray) -> float:
    """Individual Typology Angle of a mean skin colour (> 55 very light .. < -30 dark)."""
    return float(np.degrees(np.arctan2(lab[0] - 50, lab[2])))


def hexrgb(h: str) -> tuple[int, int, int]:
    return int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)


def fine(gray: np.ndarray) -> np.ndarray:
    k = np.array([1, 4, 6, 4, 1], np.float64) / 16
    b = np.apply_along_axis(lambda r: np.convolve(r, k, "same"), 1, gray)
    b = np.apply_along_axis(lambda c: np.convolve(c, k, "same"), 0, b)
    return np.abs(gray - b)


def prepare(photo: Image.Image, work: Path, landmarker, segmenter) -> tuple[np.ndarray, bool]:
    import mediapipe as mp
    faces, subject = vp.detect(photo, landmarker, segmenter)
    arr = np.asarray(photo.convert("RGB"))
    cats = np.squeeze(segmenter.segment(mp.Image(image_format=mp.ImageFormat.SRGB, data=arr)).category_mask.numpy_view())
    photo.convert("RGB").save(work / "in.ppm")
    Image.fromarray(subject).save(work / "subject.pgm")
    fm, pm = vp.draw_masks(photo.size, faces)
    fm.save(work / "faces.pgm")
    pm.save(work / "protect.pgm")
    return cats, bool(faces)


def sheet(original: Image.Image, results: list[Image.Image], shades: list[dict], path: Path, tile_h: int = 420) -> None:
    font = ImageFont.truetype(FONT, 18)
    tw = round(original.width * tile_h / original.height)
    cols = 7
    tiles = [("Original", None, original)] + [(s["name"], s["color"], r) for s, r in zip(shades, results)]
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new("RGB", (cols * tw + (cols - 1) * 6, rows * (tile_h + 34) + (rows - 1) * 6), (18, 18, 22))
    d = ImageDraw.Draw(out)
    for k, (label, color, img) in enumerate(tiles):
        x, y = (k % cols) * (tw + 6), (k // cols) * (tile_h + 40)
        out.paste(img.resize((tw, tile_h), Image.LANCZOS), (x, y))
        tx = x + 8
        if color:
            d.ellipse((x + 8, y + tile_h + 8, x + 28, y + tile_h + 28), fill=hexrgb(color), outline=(240, 240, 240))
            tx = x + 34
        d.text((tx, y + tile_h + 8), label, fill=(240, 240, 240), font=font)
    out.save(path, quality=90)


def lin(rgb: np.ndarray) -> np.ndarray:
    c = rgb.astype(np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def measure(before: np.ndarray, after: np.ndarray, cats: np.ndarray, protect: np.ndarray, gains: list[float]) -> dict:
    lb, la = srgb_to_lab(before), srgb_to_lab(after)
    de = np.linalg.norm(la - lb, axis=-1)
    out = {}
    for name, sel in (("face", (cats == FACE) & (protect < 128)), ("body", cats == BODY), ("hair", cats == HAIR),
                      ("clothes", cats == CLOTHES), ("background", cats == 0), ("features", protect >= 128)):
        if sel.sum() < 200:
            continue
        out[name] = {"change": float(de[sel].mean()), "pixels": int(sel.sum())}
        if name in ("face", "body"):
            mb, ma = lb[sel].mean(0), la[sel].mean(0)
            out[name]["L_before"], out[name]["L_after"] = float(mb[0]), float(ma[0])
            # The skin's colour change as gains in linear light, against the shade's own.
            got = lin(after[sel]).mean(0) / np.maximum(lin(before[sel]).mean(0), 1e-6)
            out[name]["gains"] = [float(v) for v in got]
            out[name]["gain_error"] = float(np.abs(np.log(got) - np.log(np.array(gains))).mean())
            gb = before[..., :3].astype(np.float64).mean(-1)
            ga = after[..., :3].astype(np.float64).mean(-1)
            out[name]["texture"] = float(fine(ga)[sel].mean() / max(fine(gb)[sel].mean(), 1e-6))
    out["clip_new"] = float(((after.max(-1) >= 255) & (before.max(-1) < 255)).mean())
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("demo")
    ap.add_argument("landmarker")
    ap.add_argument("segmenter")
    ap.add_argument("out")
    ap.add_argument("photos", nargs="*")
    ap.add_argument("--fairface")
    ap.add_argument("--count", type=int, default=16)
    ap.add_argument("--strength", type=float, default=1.0)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    shades = json.loads((here.parent / "src/services/skintones/skintones.json").read_text())["shades"]
    with open(out / "shades.txt", "w") as f:
        for s in shades:
            gr, gg, gb = s["gains"]
            f.write(f"{gr:.4f} {gg:.4f} {gb:.4f} {s['glow']:.3f}\n")

    from mediapipe.tasks.python import BaseOptions, vision
    landmarker = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=a.landmarker), num_faces=4))
    segmenter = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=a.segmenter), output_category_mask=True))

    inputs = [(Path(p).stem, ImageOps.exif_transpose(Image.open(p)).convert("RGB"), True) for p in a.photos]
    if a.fairface:
        import pyarrow.parquet as pq
        table = pq.read_table(a.fairface).to_pylist()
        step = max(1, len(table) // a.count)
        for i, row in enumerate(table[::step][: a.count]):
            img = Image.open(io.BytesIO(row["image"]["bytes"])).convert("RGB")
            inputs.append((f"ff{i:02d}", img.resize((img.width * 2, img.height * 2), Image.LANCZOS), False))

    results: dict = {"photos": {}}
    for name, photo, keep_sheet in inputs:
        work = out / name
        work.mkdir(exist_ok=True)
        cats, has_faces = prepare(photo, work, landmarker, segmenter)
        if not has_faces:
            print(f"{name}: no face, skipped")
            continue
        text = subprocess.run([a.demo, str(work / "in.ppm"), str(work / "faces.pgm"), str(work / "subject.pgm"),
                               str(work / "protect.pgm"), str(out / "shades.txt"), str(work) + "/", str(a.strength)],
                              check=True, capture_output=True, text=True).stdout
        runs = [json.loads(line) for line in text.splitlines() if line.strip()]
        before = np.asarray(photo)
        protect = np.asarray(Image.open(work / "protect.pgm"))
        imgs = [Image.open(work / f"{k:02d}.ppm") for k in range(len(shades))]
        sel = (cats == FACE) & (protect < 128)
        skin_lab = srgb_to_lab(before[sel]).mean(0) if sel.sum() > 200 else None
        rec = {"ita": ita(skin_lab) if skin_lab is not None else None, "shades": {}}
        skins = []
        for s, r, img in zip(shades, runs, imgs):
            after = np.asarray(img)
            m = measure(before, after, cats, protect, s["gains"])
            m["ms"] = r["ms"]
            m["coverage"] = r["coverage"]
            rec["shades"][s["id"]] = m
            skins.append(srgb_to_lab(after[sel]).mean(0) if sel.sum() > 200 else None)
        pairs = [(float(np.linalg.norm(skins[i] - skins[j])), shades[i]["id"], shades[j]["id"])
                 for i in range(len(skins)) for j in range(i + 1, len(skins)) if skins[i] is not None and skins[j] is not None]
        if pairs:
            rec["closest_pair"] = min(pairs)
        results["photos"][name] = rec
        if keep_sheet:
            sheet(photo, imgs, shades, out / f"sheet_{name}.jpg")
        ms = [r["ms"] for r in runs]
        print(f"{name}: {photo.size}, ITA {rec['ita']:.0f}, {min(ms):.0f}-{max(ms):.0f} ms per shade"
              + (f", closest pair {rec['closest_pair'][0]:.1f} ({rec['closest_pair'][1]}, {rec['closest_pair'][2]})" if pairs else ""))
    (out / "results.json").write_text(json.dumps(results, indent=1))
    landmarker.close()
    segmenter.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
