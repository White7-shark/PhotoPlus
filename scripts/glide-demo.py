"""
Runs a real phone video through the Glide pipeline (desktop stand-ins for
ML Kit, as in scripts/validate-portrait.py) and measures the result.

  python3 scripts/glide-demo.py VIDEO_DEMO LANDMARKER.task SEGMENTER.tflite VIDEO.mp4 OUT_DIR

Outputs in OUT_DIR:
  original.mp4         the clip as recorded (upright)
  glide.mp4            Glide: steadied, cleaned, lens-sharpened, clip-wide tone, Glow
  glide_spotlight.mp4  Glide with Spotlight (background blur, subject pop)
  compare.mp4          original | Glide | Glide + Spotlight, side by side
  report.json          stabilisation, timings and measured shake (independent: OpenCV phase correlation)

Needs ffmpeg, mediapipe, opencv-python-headless and numpy.
"""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("validate_portrait", here / "validate-portrait.py")
vp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vp)

GLOW = ["--retouch", "0.5", "--brighten", "0.3", "--eyes", "0.2", "--under-eye", "0.3", "--shine", "0.2"]
SPOTLIGHT = ["--blur", "0.55", "--pop", "0.4"]


def sh(*args: str) -> str:
    return subprocess.run(list(args), check=True, capture_output=True, text=True).stdout


def shake(frames_dir: Path, count: int) -> float:
    """Mean frame-to-frame jitter (px) of the camera path: second differences
    of the accumulated phase-correlation shift, measured independently of the
    engine (OpenCV)."""
    import cv2

    path = [np.zeros(2)]
    prev = None
    for k in range(count):
        g = cv2.imread(str(frames_dir / f"frame_{k:04d}.ppm"), cv2.IMREAD_GRAYSCALE)
        g = cv2.resize(g, (g.shape[1] // 4, g.shape[0] // 4), interpolation=cv2.INTER_AREA).astype(np.float32)
        if prev is not None:
            (dx, dy), _ = cv2.phaseCorrelate(prev, g, cv2.createHanningWindow(g.shape[::-1], cv2.CV_32F))
            path.append(path[-1] + np.array([dx, dy]) * 4)
        prev = g
    p = np.array(path)
    return float(np.mean(np.linalg.norm(p[2:] - 2 * p[1:-1] + p[:-2], axis=1)))


def main() -> int:
    demo, landmarker_path, segmenter_path, video, out = sys.argv[1:6]
    out = Path(out)
    src = out / "frames"
    src.mkdir(parents=True, exist_ok=True)
    # 1. Decode (ffmpeg applies the phone's rotation: frames come out upright).
    sh("ffmpeg", "-v", "error", "-y", "-i", video, "-start_number", "0", str(src / "frame_%04d.ppm"))
    count = len(list(src.glob("frame_*.ppm")))
    fps = sh("ffprobe", "-v", "error", "-select_streams", "v", "-show_entries", "stream=r_frame_rate", "-of",
             "csv=p=0", video).strip().strip(",").split("\n")[0].strip(",")
    num, den = (int(v) for v in fps.split("/"))

    # 2. Per frame: subject mask, faces, landmarks (smoothed over time so
    #    face touches don't jitter; the engine steadies the subject mask).
    import mediapipe as mp
    from mediapipe.tasks.python import BaseOptions, vision
    lm = vision.FaceLandmarker.create_from_options(vision.FaceLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=landmarker_path), num_faces=2,
        running_mode=vision.RunningMode.VIDEO))
    seg = vision.ImageSegmenter.create_from_options(vision.ImageSegmenterOptions(
        base_options=BaseOptions(model_asset_path=segmenter_path), output_category_mask=True,
        running_mode=vision.RunningMode.VIDEO))
    smooth = None
    try:
        for k in range(count):
            frame = Image.open(src / f"frame_{k:04d}.ppm").convert("RGB")
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(frame))
            ts = int(k * 1000 * den / num)
            faces = lm.detect_for_video(image, ts).face_landmarks
            subject = (np.squeeze(seg.segment_for_video(image, ts).category_mask.numpy_view()) > 0).astype(np.uint8) * 255
            half = (frame.width // 2, frame.height // 2)
            Image.fromarray(subject).resize(half, Image.BILINEAR).save(src / f"subject_{k:04d}.pgm")
            fm, pm = vp.draw_masks(frame.size, faces)
            fm.resize(half, Image.BILINEAR).save(src / f"faces_{k:04d}.pgm")
            pm.resize(half, Image.BILINEAR).save(src / f"protect_{k:04d}.pgm")
            if faces:
                pts = np.array(vp.landmarks_of(faces[0]))
                smooth = pts if smooth is None else 0.55 * smooth + 0.45 * pts
                (src / f"landmarks_{k:04d}.txt").write_text(",".join(f"{v:.5f}" for v in smooth) + "\n")
            else:
                (src / f"landmarks_{k:04d}.txt").write_text("")
    finally:
        lm.close()
        seg.close()

    # 3. Glide, and Glide with Spotlight.
    report = {"frames": count, "fps": fps}
    for name, extra in (("glide", GLOW), ("glide_spotlight", GLOW + SPOTLIGHT)):
        dst = out / name
        dst.mkdir(exist_ok=True)
        r = json.loads(sh(demo, str(src), str(dst), str(count), "--strength", "1", "--max-zoom", "1.12", *extra))
        report[name] = r
        report[name]["shakeMeasured"] = round(shake(dst, count), 3)
    report["original"] = {"shakeMeasured": round(shake(src, count), 3)}

    # 4. Encode with the original sound.
    def encode(frames: Path, target: Path) -> None:
        sh("ffmpeg", "-v", "error", "-y", "-framerate", fps, "-i", str(frames / "frame_%04d.ppm"), "-i", video, "-map",
           "0:v", "-map", "1:a?", "-c:v", "libx264", "-crf", "17", "-preset", "slow", "-pix_fmt", "yuv420p",
           "-c:a", "copy", "-shortest", str(target))

    encode(src, out / "original.mp4")
    encode(out / "glide", out / "glide.mp4")
    encode(out / "glide_spotlight", out / "glide_spotlight.mp4")
    font = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
    label = lambda t: f"scale=540:-2,drawtext=fontfile={font}:text='{t}':x=20:y=20:fontsize=34:fontcolor=white:box=1:boxcolor=black@0.5:boxborderw=10"
    sh("ffmpeg", "-v", "error", "-y", "-i", str(out / "original.mp4"), "-i", str(out / "glide.mp4"), "-i",
       str(out / "glide_spotlight.mp4"), "-filter_complex",
       f"[0:v]{label('Original')}[a];[1:v]{label('Glide')}[b];[2:v]{label('Glide + Spotlight')}[c];[a][b][c]hstack=inputs=3",
       "-map", "0:a?", "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "copy", str(out / "compare.mp4"))
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
