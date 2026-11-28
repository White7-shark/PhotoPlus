# ToonNet: Photo Plus's own cartoon model

No published photo-to-cartoon model has weights that are clearly free for commercial use (see `docs/FREE_PLAN_ADS_ONBOARDING_REPORT.md`). So Photo Plus trains its own. This folder is the whole pipeline: data, training, evaluation and export to the app.

## What makes it the app's own

- **One network for all settings.** It draws every style, background, ink and slider in Toon Studio, steered by the settings vector (`cond.py`, mirrored by `ToonCond` in the app). AnimeGAN-style models are one fixed look per file.
- **It reads the person's mask.** The app already has the mask, so the background can be drawn apart from the person, as the classic engine does.
- **It is built to keep identity.** It is a U-Net with the photo fed into its last layer, so faces stay the person's own.
- **Phone-sized.** It has 1.33 M weights (about 2.7 MB in fp16) and costs 17.5 GMAC at 512 × 512. Every operator converts to LiteRT and runs on its GPU delegate. AnimeGANv2's generator has more weights and more compute.
- **Clean rights.** Stage 1 learns from the app's own engine. Stage 2 learns only from art with a `LICENCE.txt`. Photos without a licence are refused. Each checkpoint and model card lists the licences of everything it learned from.

## The two stages

1. **Distil (paired).** `data.py make` draws each training photo with the classic engine under randomly sampled settings: mostly near a style's own start, as people use it, and sometimes anywhere. `train.py` then learns to reproduce the classic engine under every setting. The result draws like the app, in a single network pass.
2. **Style (unpaired, optional).** This is what can make it better than the classic engine and a match for AnimeGAN.
   - A patch critic compares ToonNet's pictures with hand-drawn art for one style.
   - Edge-smoothed copies of the art are shown to the critic as "not art". This is AnimeGAN's idea, and it is what teaches crisp lines.
   - The stage-1 pictures stay as an anchor, so faces and skin tones do not drift.

## What it needs (the parts this repository can't supply)

| Need | Why | How to get it with clear rights |
| --- | --- | --- |
| **Photos of people**: 2,000–10,000, every skin tone, ages, lighting, phones, selfies and groups | So the model generalises. The smoke test below used 4 photos. | Your own shoots with model releases, or stock bought with an explicit ML-training licence. Not FFHQ, CelebA or scraped images, whose terms forbid this or are unclear. |
| **Hand-drawn art per style**: about 300–1,000 images per style for stage 2 | The look that can rival AnimeGAN. AnimeGAN learned from film frames, which is why its weights are non-commercial. | Commission illustrators, with a contract that grants ML-training rights. |
| **Masks, faces and contours** for each photo | The teacher draws faces from them, as in the app. | Run Google's open MediaPipe or ML Kit models offline. These are Apache-licensed, but check that before use. |
| **One GPU** | My estimate: stage 1 (60 k steps, batch 16, 256 px) takes a few hours; stage 2 takes about an hour per style. | Any cloud GPU. `--device cuda` is picked automatically. |

## Commands

```bash
pip install -r requirements.txt           # torch (CPU or CUDA), numpy, pillow, onnx, onnxscript, onnxruntime
cmake -S ../../native/engine -B eng -DPP_ENGINE_BUILD_TOOLS=ON -DPP_ENGINE_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build eng --target toon_demo      # the teacher: the app's classic engine

python3 data.py make --manifest photos.jsonl --teacher eng/toon_demo --out pairs --per-photo 40 --sizes 512 768
python3 train.py --pairs pairs --out runs/distil --steps 60000 --batch 16 --crop 256
python3 train.py --pairs pairs --out runs/anime --style-dir art/anime --style 0 --resume runs/distil/last.pt --steps 20000
python3 evaluate.py --ckpt runs/anime/last.pt --pairs pairs --out eval --teacher eng/toon_demo
python3 export.py --ckpt runs/anime/last.pt --out build --size 512
python3 -m unittest test_toon_model        # the pipeline's own tests
```

For the app's LiteRT file, `export.py` also writes `toon_photoplus.tflite` when `litert-torch` is installed. It currently needs `torch<2.15` and several GB of disk, so use a separate environment: `pip install "torch<2.15" litert-torch`.

## Shipping it

1. Train both stages on licensed data. Check `evaluate.py` and the sheet with your designers.
2. Run the classic engine's acceptance checks (faces of every skin tone, skin never pushed orange, identity kept) on the model's pictures.
3. Put `toon_photoplus.tflite` and `toon_photoplus.json` (its model card) in the app's assets, or in the asset download with its sha256.
4. Measure memory and time on a low-end phone. `ModelToonEngine.SPEC` reserves 160 MB.
5. Turn on `TOON_MODEL_ENGINE` in `android/app/build.gradle`. The app falls back to the classic engine if the model fails.

## The smoke test (what was actually run here)

The pipeline was run end to end on a 4-core CPU with no GPU. The data was 4 of the owner's own test photos for training and 1 held out (480 teacher pairs at 384 and 512 px), with 3,000 steps at 192 px.
- It shows the pipeline works and that ToonNet learns the app's styles and controls.
- It is **not** a shippable model: 4 photos are far too few, and stage 2 has no art yet.
- Its weights and pictures are of real people, so they stay local and are never committed.

The results (18.0 dB against the teacher on the held-out photo, still rising) are in `docs/TOON_MODEL_REPORT.md`.
