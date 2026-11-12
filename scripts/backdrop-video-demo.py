"""
Backdrops on a real phone clip (development only, desktop): each chosen
design is applied to every frame after stabilising, with a steadied subject
mask, and encoded with the original sound; a labelled grid compares them.

  python3 scripts/backdrop-video-demo.py VIDEO_DEMO SEGMENTER.tflite VIDEO.mp4 OUT_DIR [DESIGN_INDEX ...]

Needs ffmpeg, mediapipe, numpy, pillow. MediaPipe's selfie segmenter stands
in for the phone's on-device model.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

here = Path(__file__).resolve().parent
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def sh(*args: str) -> str:
    return subprocess.run(list(args), check=True, capture_output=True, text=True).stdout


def main() -> int:
    demo, segmenter_path, video, out = sys.argv[1:5]
    designs = [int(v) for v in sys.argv[5:]] or [0, 2, 6, 7, 12]
    names = [b["name"] for b in json.loads((here.parent / "src/services/backdrops/backdrops.json").read_text())["backdrops"]]
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
    for k in range(count):
        frame = Image.open(src / f"frame_{k:04d}.ppm").convert("RGB")
        image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(frame))
        mask = (np.squeeze(seg.segment_for_video(image, int(k * 1000 * den / num)).category_mask.numpy_view()) > 0).astype(np.uint8) * 255
        Image.fromarray(mask).resize((frame.width // 2, frame.height // 2), Image.BILINEAR).save(src / f"subject_{k:04d}.pgm")
    seg.close()

    def encode(frames: Path, target: Path) -> None:
        sh("ffmpeg", "-v", "error", "-y", "-framerate", fps, "-i", str(frames / "frame_%04d.ppm"), "-i", video, "-map", "0:v",
           "-map", "1:a?", "-c:v", "libx264", "-crf", "18", "-preset", "medium", "-pix_fmt", "yuv420p", "-c:a", "copy",
           "-shortest", str(target))

    encode(src, out / "original.mp4")
    clips = [("Original", out / "original.mp4")]
    report = {}
    for d in designs:
        dst = out / f"d{d:02d}"
        dst.mkdir(exist_ok=True)
        report[names[d]] = json.loads(sh(demo, str(src), str(dst), str(count), "--strength", "1", "--max-zoom", "1.12",
                                         "--backdrop", str(d)))
        target = out / f"backdrop_{names[d].lower().replace(' ', '_')}.mp4"
        encode(dst, target)
        Image.open(dst / f"frame_{count // 2:04d}.ppm").save(out / f"mid_{d:02d}.jpg", quality=90)
        shutil.rmtree(dst)  # frames are large; the clip is what is kept
        clips.append((names[d], target))
    Image.open(src / f"frame_{count // 2:04d}.ppm").save(out / "mid_original.jpg", quality=90)
    shutil.rmtree(src)

    # A labelled grid of every clip.
    cols = 3
    rows = (len(clips) + cols - 1) // cols
    inputs, filters, tags = [], [], []
    for i, (label, path) in enumerate(clips):
        inputs += ["-i", str(path)]
        filters.append(f"[{i}:v]scale=360:-2,drawtext=fontfile={FONT}:text='{label}':x=12:y=12:fontsize=24:fontcolor=white:"
                       f"box=1:boxcolor=black@0.5:boxborderw=8[v{i}]")
        tags.append(f"[v{i}]")
    layout = "|".join(f"{(i % cols)}*w0_{(i // cols)}*h0".replace("0*w0", "0").replace("0*h0", "0").replace("1*w0", "w0").replace("2*w0", "w0+w0").replace("1*h0", "h0") for i in range(len(clips)))
    grid = ";".join(filters) + ";" + "".join(tags) + f"xstack=inputs={len(clips)}:layout={layout}:fill=black[out]"
    sh("ffmpeg", "-v", "error", "-y", *inputs, "-filter_complex", grid, "-map", "[out]", "-map", "0:a?", "-c:v", "libx264",
       "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "copy", str(out / "compare.mp4"))
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
