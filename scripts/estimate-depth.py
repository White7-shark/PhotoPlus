"""
Relative depth for the Spotlight preview, made with the same model the app
bundles (Depth Anything V2 Small, LiteRT, 8-bit weights; fetched by the
Android build, see android/app/build.gradle) and the same pre/post-processing
as DepthEstimator.kt / DepthMath.kt: portrait photos are turned landscape,
ImageNet-normalised, and the output stretched between its 1st and 99th
percentiles (0 far ... 255 near).

  pip install ai-edge-litert numpy pillow
  python3 scripts/estimate-depth.py MODEL.tflite IN.png OUT.pgm
"""
import sys
import time

import numpy as np
from ai_edge_litert.interpreter import Interpreter
from PIL import Image

model, src, out = sys.argv[1:4]
img = Image.open(src).convert("RGB")
w, h = img.size
portrait = h > w
work = img.rotate(90, expand=True) if portrait else img  # anticlockwise, like DepthEstimator
x = np.asarray(work.resize((686, 518), Image.BILINEAR), dtype=np.float32) / 255.0
x = (x - [0.485, 0.456, 0.406]) / [0.229, 0.224, 0.225]
x = x.transpose(2, 0, 1)[None].astype(np.float32)
it = Interpreter(model_path=model, num_threads=4)
it.allocate_tensors()
it.set_tensor(it.get_input_details()[0]["index"], x)
t = time.time()
it.invoke()
ms = (time.time() - t) * 1000
d = np.squeeze(it.get_tensor(it.get_output_details()[0]["index"]))
lo, hi = np.percentile(d, 1), np.percentile(d, 99)
g = Image.fromarray((np.clip((d - lo) / (hi - lo), 0, 1) * 255).astype(np.uint8))
if portrait:
    g = g.rotate(-90, expand=True)
g.save(out)
print(f"{src}: depth in {ms:.0f} ms")
