"""
Renders every Backdrop on real photos (development only, desktop).

MediaPipe's selfie segmenter stands in for the phone's on-device model, and
the bundled depth model (optional) for Signature's depth-aware blur.

  python3 scripts/backdrop-demo.py BACKDROP_DEMO SEGMENTER.tflite OUT_DIR [--depth DEPTH.tflite] PHOTO ...

Writes OUT_DIR/<photo>/NN.ppm per design and a labelled sheet per photo.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps

here = Path(__file__).resolve().parent
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def names() -> list[str]:
    data = json.loads((here.parent / "src/services/backdrops/backdrops.json").read_text())
    return [b["name"] for b in data["backdrops"]]


def subject_mask(photo: Image.Image, segmenter) -> Image.Image:
    import mediapipe as mp
    image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(photo.convert("RGB")))
    seg = np.squeeze(segmenter.segment(image).category_mask.numpy_view())
    return Image.fromarray((seg > 0).astype(np.uint8) * 255)


def sheet(original: Image.Image, tiles: list[tuple[str, Image.Image]], path: Path, cols: int = 5, tw: int = 360) -> None:
    tiles = [("Original", original)] + tiles
    th = int(tw * original.height / original.width)
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new("RGB", (cols * (tw + 6), rows * (th + 6)), (14, 14, 20))
    font = ImageFont.truetype(FONT, 19)
    for i, (label, img) in enumerate(tiles):
        t = img.convert("RGB").resize((tw, th), Image.LANCZOS)
        d = ImageDraw.Draw(t)
        d.rectangle([0, th - 30, tw, th], fill=(0, 0, 0))
        d.text((8, th - 27), label, font=font, fill=(255, 255, 255))
        out.paste(t, ((i % cols) * (tw + 6), (i // cols) * (th + 6)))
    out.save(path, quality=90)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("demo")
    ap.add_argument("segmenter")
    ap.add_argument("out")
    ap.add_argument("photos", nargs="+")
    ap.add_argument("--depth")
    ap.add_argument("--max-edge", type=int, default=2048)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    from mediapipe.tasks.python import BaseOptions, vision
    segmenter = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=a.segmenter), output_category_mask=True))
    labels = names()
    for p in a.photos:
        photo = ImageOps.exif_transpose(Image.open(p)).convert("RGB")  # upright, as the phone shows it
        photo.thumbnail((a.max_edge, a.max_edge), Image.LANCZOS)
        work = out / Path(p).stem
        work.mkdir(exist_ok=True)
        photo.save(work / "in.ppm")
        photo.save(work / "in.png")
        subject_mask(photo, segmenter).save(work / "subject.pgm")
        depth = "-"
        if a.depth:
            subprocess.run([sys.executable, str(here / "estimate-depth.py"), a.depth, str(work / "in.png"), str(work / "depth.pgm")],
                           check=True, capture_output=True)
            depth = str(work / "depth.pgm")
        log = subprocess.run([a.demo, str(work / "in.ppm"), str(work / "subject.pgm"), depth, str(work) + "/"],
                             check=True, capture_output=True, text=True).stdout
        (work / "report.jsonl").write_text(log)
        tiles = [(labels[i], Image.open(work / f"{i:02d}.ppm")) for i in range(len(labels))]
        sheet(photo, tiles, out / f"sheet_{Path(p).stem}.jpg")
        ms = [json.loads(l)["ms"] for l in log.splitlines()]
        print(f"{Path(p).stem}: {photo.size}, {min(ms):.0f}-{max(ms):.0f} ms per design", file=sys.stderr)
    segmenter.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
