# ToonNet: Photo Plus's own cartoon model

## Why

No published photo-to-cartoon model has weights that are clearly free for commercial use. AnimeGANv2/v3 and White-box Cartoonization are non-commercial, and the others have unclear training data (see `FREE_PLAN_ADS_ONBOARDING_REPORT.md`). So the app gets a model of its own, trained only on data Photo Plus has the rights to.

## What was built (`tools/toon_model`)

| Part | What it does |
| --- | --- |
| `model.py` | **ToonNet**: a U-Net with 1.33 M weights that costs 17.5 GMAC at 512². <br>It reads the photo, the person mask, two coordinate channels and the 31-value settings vector (FiLM on 8 depthwise-separable residual blocks). <br>It uses only operators LiteRT handles on the GPU. <br>`PatchCritic` is the discriminator for stage 2. |
| `cond.py` | The app's 18 Toon settings encoded as 31 values, matched exactly by `ToonCond` in the app (tested on both sides). |
| `data.py` | Teacher pairs drawn by the app's classic engine under sampled settings. A manifest row without a licence is refused. |
| `train.py` | **Stage 1, distil**: L1, edge and multi-scale losses, with an EMA of the weights. <br>**Stage 2, style**: an LSGAN against licensed art, with edge-smoothed negatives and the teacher as an anchor. Art without a `LICENCE.txt` is refused. |
| `evaluate.py` | On held-out photos: PSNR against the teacher, line-art F1, skin hue shift, CPU time, and a comparison sheet. |
| `export.py` | ONNX in the app's NHWC layout (`image`, `mask`, `cond` → `toon`), checked against onnxruntime, plus a model card listing the data licences. Writes `.tflite` when `litert-torch` is installed. |
| `test_toon_model.py` | 12 tests: the settings vector, the catalogue, ranges, shapes, steering, the losses, the licence checks, and ONNX parity. |

**App wiring:**
- `ToonCond` encodes the settings, and `InferenceSession.run(bitmap, extra)` takes several inputs.
- `ModelToonEngine` feeds the photo, mask and settings to `toon_photoplus.tflite`.
- It stays behind `TOON_MODEL_ENGINE = false`, and the classic engine remains the fallback.

## The smoke run (4-core CPU, no GPU)

**Setup:**
- 5 of the owner's own test photos: 4 for training and 1 held out. They stay local and are never committed.
- 480 teacher pairs at 384 and 512 px.
- 3,000 steps, batch 8, 192 px crops, at about 1.1 s per step (56 minutes).

**Training:**
- Held-out PSNR by step: 17.05 (500), 17.16 (1,000), 17.35 (1,500), 17.59 (2,000), 18.01 (3,000). It was still rising when the run stopped.

**Held out (96 pairs):**
- PSNR 17.9 dB; mean error 25 levels out of 255.
- Line-art F1 0.13.
- Skin hue shift 23°, against 9.5° for the teacher.
- By style: Anime 18.5, Manga 16.8, Comic 16.9, Cartoon 16.2, Webtoon 19.4, Retro 19.3 dB.

**Speed and export:**
- 334 ms per 512² picture on this CPU (PyTorch). A phone GPU via LiteRT should be faster, but that wasn't measured.
- ONNX matches PyTorch to within 4e-7. The file is 7.6 MB (fp32).

**What the sheet shows:**
- The network learned the mapping. It keeps the face and identity, and it learned Manga's black-and-white ink look, so the settings do steer it.
- The other styles are still soft and close to one another.
- It does not yet draw the classic engine's crisp lines (F1 0.13) or flat shading.
- That is expected with 4 photos and 3,000 small CPU steps. **This run proves the pipeline end to end; it is not a model to ship.**

## What it takes to rival AnimeGAN

1. **Data with rights:**
   - 2,000–10,000 photos of people with model releases (every skin tone, age and lighting condition);
   - 300–1,000 commissioned illustrations per style, with ML-training rights.
2. **Stage 1** on a GPU (60 k steps, batch 16, 256 px; my estimate is a few hours). This should reach the classic engine's look: crisp lines and flat shading.
3. **Stage 2** for each style on the commissioned art (about 20 k steps). This is the step that adds a hand-drawn quality beyond what the classic engine does.
4. **Before turning on the flag:**
   - check faces of every skin tone with the acceptance checks (skin hue shift close to the teacher's);
   - measure memory and time on a low-end phone;
   - convert to LiteRT with `litert-torch` (it needs `torch<2.15`).
