"""
Runs the 13 beauty presets on real photos (development only, desktop) and
measures what decides whether each one works:

  - distinct: every preset gives a visibly different photo (mean CIELAB
    difference between any two presets' results, over the whole frame);
  - skin kept: the hue of the person's own skin on the cheeks (CIELAB h_ab,
    where the skin has colour) moves only as much as the look intends (blush
    and warmth add a little; a lift keeps hue), grouped by measured
    skin tone (Individual Typology Angle) when FairFace faces are given;
  - texture kept: fine skin texture on the cheeks (real skin, not plastic);
  - no clipping: highlights never newly clip.

MediaPipe stands in for ML Kit (face mesh mapped to the app's 131-point
layout, selfie segmentation for the subject), as in validate-portrait.py.
The recipes come from the app's own resolver (scripts/beauty-recipes.mts).

  python3 scripts/beauty-demo.py BEAUTY_DEMO LANDMARKER.task SEGMENTER.tflite OUT_DIR \\
      [--fairface VALIDATION.parquet --count 24] [--depth-blur 0.6] PHOTO ...

Writes OUT_DIR/results.json and a labelled sheet per photo (original + 13).
"""
from __future__ import annotations

import argparse
import importlib.util
import io
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("validate_portrait", here / "validate-portrait.py")
vp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vp)

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def recipes(out: Path, depth_blur: float | None) -> list[str]:
    cmd = ["node", "--experimental-strip-types", "--no-warnings", str(here / "beauty-recipes.mts"), "--none"]
    if depth_blur is not None:
        cmd += ["--depth-blur", str(depth_blur)]
    text = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    (out / "recipes.txt").write_text(text)
    return [line.split()[0] for line in text.splitlines() if line.strip()]


def prepare(photo: Image.Image, work: Path, landmarker, segmenter) -> tuple[list, bool]:
    faces, subject = vp.detect(photo, landmarker, segmenter)
    photo.convert("RGB").save(work / "in.ppm")
    Image.fromarray(subject).save(work / "subject.pgm")
    fm, pm = vp.draw_masks(photo.size, faces)
    fm.save(work / "faces.pgm")
    pm.save(work / "protect.pgm")
    with open(work / "landmarks.txt", "w") as f:
        for face in faces:
            f.write(",".join(f"{v:.5f}" for v in vp.landmarks_of(face)) + "\n")
    return faces, bool(faces)


def run(demo: str, work: Path, recipes_file: Path, has_faces: bool) -> dict:
    out = subprocess.run([demo, str(work / "in.ppm"), str(work / "subject.pgm"), str(work / "faces.pgm"),
                          str(work / "protect.pgm"), str(work / "landmarks.txt") if has_faces else "-",
                          str(recipes_file), str(work) + "/out_"], check=True, capture_output=True, text=True).stdout
    rows = [json.loads(l) for l in out.splitlines() if l.startswith("{")]
    for r in rows:
        if "scene" in r:  # what the grade measured (the live shader's scene uniforms)
            (work / "scene.json").write_text(json.dumps(r["scene"]))
    return {r["id"]: r for r in rows if "id" in r}


def lab_image(img: Image.Image, size: int = 256) -> np.ndarray:
    small = img.convert("RGB").resize((size, int(size * img.height / img.width)), Image.BILINEAR)
    a = np.asarray(small).astype(np.float64) / 255.0
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = lin @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def cheek_stats(img: Image.Image, cheeks: np.ndarray) -> tuple[np.ndarray, float]:
    """Median CIELAB colour of the cheeks, and their fine texture."""
    arr = np.asarray(img.convert("RGB"))
    y = vp.ycc(arr)[..., 0]
    return vp.lab(arr[cheeks]), vp.fine_texture(y, cheeks)


def sheet(original: Image.Image, results: dict[str, Image.Image], names: dict[str, str], path: Path) -> None:
    tiles = [("Original", original)] + [(names[k], v) for k, v in results.items()]
    tw = 300
    th = int(tw * original.height / original.width)
    cols = 7
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new("RGB", (cols * (tw + 6), rows * (th + 6)), (18, 18, 24))
    font = ImageFont.truetype(FONT, 17)
    for i, (label, img) in enumerate(tiles):
        t = img.convert("RGB").resize((tw, th), Image.LANCZOS)
        d = ImageDraw.Draw(t)
        d.rectangle([0, th - 28, tw, th], fill=(0, 0, 0))
        d.text((8, th - 25), label, font=font, fill=(255, 255, 255))
        out.paste(t, ((i % cols) * (tw + 6), (i // cols) * (th + 6)))
    out.save(path, quality=90)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("demo")
    ap.add_argument("landmarker")
    ap.add_argument("segmenter")
    ap.add_argument("out")
    ap.add_argument("photos", nargs="*")
    ap.add_argument("--fairface")
    ap.add_argument("--count", type=int, default=24)
    ap.add_argument("--depth-blur", type=float)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ids = recipes(out, a.depth_blur)
    data = json.loads((here.parent / "src/services/beauty/presets.json").read_text())
    names = {p["id"]: p["name"] for p in data["presets"]}

    import mediapipe as mp
    from mediapipe.tasks.python import BaseOptions, vision
    landmarker = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=a.landmarker), num_faces=4))
    segmenter = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=a.segmenter), output_category_mask=True))

    inputs: list[tuple[str, Image.Image, bool]] = [(Path(p).stem, Image.open(p).convert("RGB"), True) for p in a.photos]
    if a.fairface:
        import pyarrow.parquet as pq
        table = pq.read_table(a.fairface).to_pylist()
        step = max(1, len(table) // a.count)
        for i, row in enumerate(table[::step][: a.count]):
            img = Image.open(io.BytesIO(row["image"]["bytes"])).convert("RGB")
            inputs.append((f"ff{i:02d}", img.resize((img.width * 2, img.height * 2), Image.LANCZOS), False))

    results = {"presets": {}, "photos": {}}
    per_preset: dict[str, dict] = {k: {"hue": {}, "texture": [], "clip": []} for k in ids if k != "none"}
    for name, photo, keep_sheet in inputs:
        work = out / name
        work.mkdir(exist_ok=True)
        faces, has_faces = prepare(photo, work, landmarker, segmenter)
        rep = run(a.demo, work, out / "recipes.txt", has_faces)
        imgs = {k: Image.open(work / f"out_{k}.ppm").convert("RGB") for k in ids}
        labs = {k: lab_image(v) for k, v in imgs.items() if k != "none"}
        keys = list(labs)
        pair = min(float(np.mean(np.linalg.norm(labs[i] - labs[j], axis=-1)))
                   for x, i in enumerate(keys) for j in keys[x + 1:])
        vs_original = {k: float(np.mean(np.linalg.norm(labs[k] - lab_image(photo), axis=-1))) for k in keys}
        face_px = None
        if has_faces:
            fm = np.asarray(Image.open(work / "faces.pgm").resize(labs[keys[0]].shape[1::-1], Image.BILINEAR)) > 127
            if fm.any():
                face_px = fm
        pair_face = min(float(np.mean(np.linalg.norm(labs[i][face_px] - labs[j][face_px], axis=-1)))
                        for x, i in enumerate(keys) for j in keys[x + 1:]) if face_px is not None else None
        closest = min(((float(np.mean(np.linalg.norm(labs[i] - labs[j], axis=-1))), i, j)
                       for x, i in enumerate(keys) for j in keys[x + 1:]))
        info = {"faces": len(faces), "minPairDeltaE": round(pair, 2), "closestPair": closest[1:],
                "minPairDeltaEFace": round(pair_face, 2) if pair_face is not None else None,
                "deltaEvsOriginal": {k: round(v, 1) for k, v in vs_original.items()},
                "ms": {k: rep[k]["ms"] for k in keys}}
        if has_faces:
            # Cheek areas inside the face outline only (on a turned face the
            # outer cheek disk can fall on the background).
            ch = vp.cheeks(faces, photo.size) & (np.asarray(Image.open(work / "faces.pgm")) > 127)
            lab0, t0 = cheek_stats(photo, ch)
            tone = vp.ita_group(vp.lab_ita(np.asarray(photo)[ch])) if ch.any() else "unknown"
            info["tone"] = tone
            for k in keys:
                lab1, t1 = cheek_stats(imgs[k], ch)
                # Colour moved on the cheeks (a*, b*), lightness apart: a lift is meant.
                # Hue angle of the skin (CIELAB h_ab): a lift or deeper colour keeps it;
                # a cast moves it. Only meaningful where the skin has real chroma.
                c0, c1 = float(np.hypot(lab0[1], lab0[2])), float(np.hypot(lab1[1], lab1[2]))
                if c0 >= 8:
                    shift = abs((np.degrees(np.arctan2(lab1[2], lab1[1]) - np.arctan2(lab0[2], lab0[1])) + 180) % 360 - 180)
                    per_preset[k]["hue"].setdefault(tone, []).append(float(shift))
                    per_preset[k].setdefault("chroma", []).append(c1 / c0)
                    info.setdefault("cheekHueShift", {})[k] = round(float(shift), 1)
                if t0 > 0:
                    per_preset[k]["texture"].append(t1 / t0)
                per_preset[k]["clip"].append(rep[k]["clippedAfter"] - rep[k]["clippedBefore"])
        results["photos"][name] = info
        if keep_sheet:
            sheet(photo, {k: imgs[k] for k in keys}, names, out / f"sheet_{name}.jpg")
        print(f"{name}: faces {len(faces)}, closest two presets differ by {pair:.1f} dE", file=sys.stderr)

    for k, v in per_preset.items():
        results["presets"][k] = {
            "name": names[k],
            "maxCheekHueShiftByTone": {t: round(max(h), 1) for t, h in v["hue"].items()},
            "meanCheekHueShift": round(float(np.mean([x for h in v["hue"].values() for x in h])), 2) if v["hue"] else None,
            "medianChromaRatio": round(float(np.median(v["chroma"])), 2) if v.get("chroma") else None,
            "textureKept": round(float(np.median(v["texture"])), 2) if v["texture"] else None,
            "worstNewClipping": round(max(v["clip"]), 4) if v["clip"] else None,
        }
    landmarker.close()
    segmenter.close()
    (out / "results.json").write_text(json.dumps(results, indent=1))
    print(json.dumps(results["presets"], indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
