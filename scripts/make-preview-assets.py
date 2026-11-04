"""
Builds the photo assets for the Phase 2 design preview from REAL engine
output: a simulated weak-camera burst of a Kodak image (same model as
scripts/enhance-benchmark.py) run through enhance_bench, plus a focus-peaking
mask computed the way pp_focus_peaking does.

  python3 scripts/make-preview-assets.py KODAK_PNG ENHANCE_BENCH OUT_DIR
"""
import importlib.util, json, sys, subprocess
from pathlib import Path
import numpy as np
from PIL import Image

repo = Path(__file__).resolve().parent.parent
source, bench, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("bm", repo / "scripts/enhance-benchmark.py")
bm = importlib.util.module_from_spec(spec); spec.loader.exec_module(bm)

truth = np.asarray(Image.open(source).convert("RGB"), dtype=np.float64) / 255.0
frames = []
for i in range(bm.FRAMES):
    dx, dy = (0.0, 0.0) if i == 0 else bm.RNG.uniform(-bm.MAX_SHIFT, bm.MAX_SHIFT, 2)
    f = out / f"frame{i}.ppm"
    bm.write_ppm(f, bm.capture(truth, dx, dy))
    frames.append(f)
res = subprocess.run([str(bench), "--upscale", "2", str(out / "enhanced.ppm"), *map(str, frames)], check=True, capture_output=True, text=True)
report = json.loads(res.stdout.strip().splitlines()[-1])
(out / "report.json").write_text(json.dumps(report))
weak = Image.open(frames[0]).convert("RGB")
weak.save(out / "weak.png")
weak.resize((truth.shape[1], truth.shape[0]), Image.BICUBIC).save(out / "weak-upscaled.png")
Image.open(out / "enhanced.ppm").convert("RGB").save(out / "enhanced.png")
Image.fromarray((truth * 255).astype(np.uint8)).save(out / "scene.jpg", quality=92)

# Focus-peaking overlay exactly as the app makes it: a ~640 px analysis frame,
# Sobel max-pooled per 4x4 cell, default threshold 0.4 (FocusPeakingAnalyzer).
small = Image.open(source).convert("L")
small = small.resize((640, round(640 * small.height / small.width)) if small.width >= small.height else (round(640 * small.width / small.height), 640))
y = np.asarray(small, dtype=np.float64)
gx = np.zeros_like(y); gy = np.zeros_like(y)
gx[1:-1, 1:-1] = (y[:-2, 2:] + 2 * y[1:-1, 2:] + y[2:, 2:]) - (y[:-2, :-2] + 2 * y[1:-1, :-2] + y[2:, :-2])
gy[1:-1, 1:-1] = (y[2:, :-2] + 2 * y[2:, 1:-1] + y[2:, 2:]) - (y[:-2, :-2] + 2 * y[:-2, 1:-1] + y[:-2, 2:])
mag = np.hypot(gx, gy)
h, w = mag.shape[0] // 4, mag.shape[1] // 4
pooled = mag[: h * 4, : w * 4].reshape(h, 4, w, 4).max(axis=(1, 3))
mask = pooled > 0.4 * 4 * 255
rgba = np.zeros((h, w, 4), np.uint8); rgba[mask] = (255, 59, 107, 255)
Image.fromarray(rgba, "RGBA").save(out / "peaking.png")

# Low-light viewfinder for the Night screen.
dark = np.clip(truth * 0.28 + bm.RNG.normal(size=truth.shape) * 0.035, 0, 1)
Image.fromarray((dark * 255).astype(np.uint8)).save(out / "night.jpg", quality=90)
print(json.dumps(report))
