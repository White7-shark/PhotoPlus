"""
Runs a real phone photo through the app's photo pipeline (desktop stand-ins
for ML Kit, as in scripts/validate-portrait.py) and saves every stage:

  in        the photo as taken (EXIF orientation applied)
  clarity   Clarity, the camera's default single-photo enhancement (pp_enhance)
  glow      + camera Glow "Smooth" (skin, eyes, under-eyes, shine, face light)
  advanced  + Premium fine-tuning (skin, eyes, teeth, lips, under-eyes, light)
  spotlight Spotlight on the Clarity photo: depth-aware blur (the app's depth
            model), Glow Natural, subject clarity
  colorpop  Spotlight with a black-and-white background and edge refinement

  python3 scripts/real-photo-demo.py OPS_PROBE PORTRAIT_DEMO LANDMARKER.task SEGMENTER.tflite DEPTH.tflite PHOTO OUT_DIR

The depth model runs through scripts/estimate-depth.py (needs ai-edge-litert).
Prints one JSON line per stage with the engine's report.
"""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageOps

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("validate_portrait", here / "validate-portrait.py")
vp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vp)

GLOW_SMOOTH = dict(retouch=0.75, brighten=0.4, eyes=0.3, under_eye=0.45, shine=0.35, face_light=0.2)
ADVANCED = dict(retouch=0, smooth=0.5, blemish=1.0, even=0.6, brighten=0.35, eyes=0.6, teeth=0.5, lips=0.3,
                under_eye=0.7, shine=0.7, face_light=0.5, studio=0.4, light_angle=-0.4)


def main() -> int:
    probe, demo, landmarker_path, segmenter_path, depth_model, photo_path, out = sys.argv[1:8]
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    photo = ImageOps.exif_transpose(Image.open(photo_path)).convert("RGB")
    photo.save(out / "in.png")

    # 1. Clarity.
    r = subprocess.run([probe, "enhance", str(out / "in.png"), str(out / "clarity.png")], check=True,
                       capture_output=True, text=True).stdout.strip()
    print(json.dumps({"stage": "clarity", **json.loads(r)}))
    clarity = Image.open(out / "clarity.png").convert("RGB")
    clarity.save(out / "clarity.ppm")

    # 2. Faces and subject (on the Clarity photo: the same geometry).
    import mediapipe as mp  # noqa: F401  (imported by vp.detect)
    from mediapipe.tasks.python import BaseOptions, vision
    lm = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=landmarker_path), num_faces=4))
    seg = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=segmenter_path), output_category_mask=True))
    try:
        faces, subject = vp.detect(clarity, lm, seg)
    finally:
        lm.close()
        seg.close()
    Image.fromarray(subject).save(out / "subject.pgm")
    fm, pm = vp.draw_masks(clarity.size, faces)
    fm.save(out / "faces.pgm")
    pm.save(out / "protect.pgm")
    (out / "landmarks.txt").write_text("\n".join(",".join(f"{v:.5f}" for v in vp.landmarks_of(f)) for f in faces) + "\n")
    masks = {"subject": out / "subject.pgm", "faces": out / "faces.pgm", "protect": out / "protect.pgm"}
    print(json.dumps({"stage": "faces", "count": len(faces)}))

    # 3. Depth (the app's model), as Spotlight uses it.
    subprocess.run([sys.executable, str(here / "estimate-depth.py"), depth_model, str(out / "clarity.png"),
                    str(out / "depth.pgm")], check=True, capture_output=True)

    def stage(name: str, src: Path, cfg: dict, extra: list[str] | None = None) -> Path:
        args = [demo, str(src), str(out / f"{name}.ppm"), "--subject-mask", str(masks["subject"]),
                "--faces-mask", str(masks["faces"]), "--protect-mask", str(masks["protect"]),
                "--landmarks", str(out / "landmarks.txt")]
        for k, v in cfg.items():
            args += [vp.FLAGS.get(k, "--" + k.replace("_", "-")), str(v)]
        rep = subprocess.run(args + (extra or []), check=True, capture_output=True, text=True).stdout.strip()
        print(json.dumps({"stage": name, **json.loads(rep)}))
        Image.open(out / f"{name}.ppm").save(out / f"{name}.png")
        return out / f"{name}.ppm"

    stage("glow", out / "clarity.ppm", {**GLOW_SMOOTH, "blur": 0, "pop": 0})
    stage("advanced", out / "clarity.ppm", {**ADVANCED, "blur": 0, "pop": 0})
    depth = ["--depth-mask", str(out / "depth.pgm")]
    stage("spotlight", out / "clarity.ppm", {"blur": 0.6, "retouch": 0.5, "brighten": 0.3, "pop": 0.5}, depth)
    stage("colorpop", out / "clarity.ppm",
          {"blur": 0.6, "retouch": 0.5, "brighten": 0.3, "pop": 0.5, "bg_color": -1, "edge": 1}, depth)
    return 0


if __name__ == "__main__":
    sys.exit(main())
