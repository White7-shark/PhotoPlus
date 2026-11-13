"""
Skin Tone on a real phone clip (development only, desktop): each chosen shade
is applied to every frame (with that frame's face and features, and a
steadied subject mask), the clip is stabilised as Glide does, and encoded with
the original sound; a labelled grid compares them.

  python3 scripts/skintone-video-demo.py VIDEO_DEMO LANDMARKER.task SEGMENTER.tflite VIDEO.mp4 OUT_DIR [SHADE_ID ...]

Needs ffmpeg, mediapipe, numpy, pillow. MediaPipe stands in for ML Kit.
"""
from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

here = Path(__file__).resolve().parent
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
spec = importlib.util.spec_from_file_location("validate_portrait", here / "validate-portrait.py")
vp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vp)


def sh(*args: str) -> str:
    return subprocess.run(list(args), check=True, capture_output=True, text=True).stdout


def main() -> int:
    demo, landmarker_path, segmenter_path, video, out = sys.argv[1:6]
    shades = json.loads((here.parent / "src/services/skintones/skintones.json").read_text())["shades"]
    by_id = {s["id"]: s for s in shades}
    chosen = sys.argv[6:] or ["ivory", "honey", "caramel", "cinnamon"]
    out = Path(out)
    src = out / "frames"
    src.mkdir(parents=True, exist_ok=True)
    sh("ffmpeg", "-v", "error", "-y", "-i", video, "-start_number", "0", str(src / "frame_%04d.ppm"))
    count = len(list(src.glob("frame_*.ppm")))
    fps = sh("ffprobe", "-v", "error", "-select_streams", "v", "-show_entries", "stream=r_frame_rate", "-of", "csv=p=0",
             video).strip().strip(",").split("\n")[0].strip(",")
    num, den = (int(v) for v in fps.split("/"))

    import mediapipe as mp
    from mediapipe.tasks.python import BaseOptions, vision
    seg = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=segmenter_path), output_category_mask=True,
        running_mode=vision.RunningMode.VIDEO))
    lm = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=landmarker_path), running_mode=vision.RunningMode.VIDEO))
    with_face = 0
    for k in range(count):
        frame = Image.open(src / f"frame_{k:04d}.ppm").convert("RGB")
        image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(frame))
        ts = int(k * 1000 * den / num)
        mask = (np.squeeze(seg.segment_for_video(image, ts).category_mask.numpy_view()) > 0).astype(np.uint8) * 255
        half = (frame.width // 2, frame.height // 2)
        Image.fromarray(mask).resize(half, Image.BILINEAR).save(src / f"subject_{k:04d}.pgm")
        faces = lm.detect_for_video(image, ts).face_landmarks
        if faces:
            with_face += 1
            fm, pm = vp.draw_masks(frame.size, faces)
            fm.resize(half, Image.BILINEAR).save(src / f"faces_{k:04d}.pgm")
            pm.resize(half, Image.BILINEAR).save(src / f"protect_{k:04d}.pgm")
    seg.close()
    lm.close()

    def encode(frames: Path, target: Path) -> None:
        sh("ffmpeg", "-v", "error", "-y", "-framerate", fps, "-i", str(frames / "frame_%04d.ppm"), "-i", video, "-map", "0:v",
           "-map", "1:a?", "-c:v", "libx264", "-crf", "18", "-preset", "medium", "-pix_fmt", "yuv420p", "-c:a", "copy",
           "-shortest", str(target))

    encode(src, out / "original.mp4")
    clips = [("Original", out / "original.mp4")]
    report = {"frames": count, "frames_with_face": with_face}
    # Glide alone (steadied and enhanced, no shade): the fair reference for the shades.
    base = out / "glide"
    base.mkdir(exist_ok=True)
    report["Glide"] = json.loads(sh(demo, str(src), str(base), str(count), "--strength", "1", "--max-zoom", "1.12"))
    encode(base, out / "glide.mp4")
    Image.open(base / f"frame_{count // 2:04d}.ppm").save(out / "mid_glide.jpg", quality=90)
    shutil.rmtree(base)
    clips.append(("Glide only", out / "glide.mp4"))
    for sid in chosen:
        s = by_id[sid]
        r, g, b = s["gains"]
        dst = out / sid
        dst.mkdir(exist_ok=True)
        report[s["name"]] = json.loads(sh(demo, str(src), str(dst), str(count), "--strength", "1", "--max-zoom", "1.12",
                                          "--skin-tone", f"{r:.4f}", f"{g:.4f}", f"{b:.4f}", str(s["glow"])))
        target = out / f"skintone_{sid}.mp4"
        encode(dst, target)
        Image.open(dst / f"frame_{count // 2:04d}.ppm").save(out / f"mid_{sid}.jpg", quality=90)
        shutil.rmtree(dst)
        clips.append((s["name"], target))
    Image.open(src / f"frame_{count // 2:04d}.ppm").save(out / "mid_original.jpg", quality=90)
    shutil.rmtree(src)

    cols = 3
    inputs, filters, tags, layout = [], [], [], []
    for i, (label, path) in enumerate(clips):
        inputs += ["-i", str(path)]
        filters.append(f"[{i}:v]scale=360:-2,drawtext=fontfile={FONT}:text='{label}':x=12:y=12:fontsize=24:fontcolor=white:"
                       f"box=1:boxcolor=black@0.5:boxborderw=8[v{i}]")
        tags.append(f"[v{i}]")
        cx, cy = i % cols, i // cols
        layout.append(("+".join(["w0"] * cx) or "0") + "_" + ("+".join(["h0"] * cy) or "0"))
    grid = ";".join(filters) + ";" + "".join(tags) + f"xstack=inputs={len(clips)}:layout={'|'.join(layout)}:fill=black[out]"
    sh("ffmpeg", "-v", "error", "-y", *inputs, "-filter_complex", grid, "-map", "[out]", "-map", "0:a?", "-c:v", "libx264",
       "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "copy", str(out / "compare.mp4"))
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
