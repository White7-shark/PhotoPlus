"""Training pairs for ToonNet, drawn by the app's own classic Toon engine.

    python3 data.py make --manifest photos.jsonl --teacher ../../native/engine/build/toon_demo \
        --out pairs/ --per-photo 40 --sizes 512 768

The manifest has one JSON object per line, one per photo:

    {"image": "photos/a.jpg",        # any size; RGB
     "mask": "photos/a_mask.png",    # optional: the person, white on black
     "faces": [[x, y, w, h, ...]],   # optional: 12 floats per face (box, mouth box, eye centres), 0..1
     "contours": [[x, y, ...]],      # optional: ML Kit's contour points per face, 0..1
     "licence": "...",               # required: why we may train on it (e.g. "model release #123")
     "split": "train"}               # or "val" (held out, for evaluation only)

A photo without a "licence" is refused: every picture the model learns from
must be one Photo Plus has the rights to train on. The teacher's pictures are
the app's own work, so the pairs add no rights of their own.

Each photo is saved once per size (photo + mask), and each of its pairs as
the teacher's picture and the 18 settings it was drawn with.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

from cond import PARAMS, cond_from_params, sample_params


def read_manifest(path: str) -> list[dict]:
    rows = []
    base = Path(path).parent
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip():
            continue
        row = json.loads(line)
        if not str(row.get("licence", "")).strip():
            raise SystemExit(f"{path}:{n}: no licence for {row.get('image')}; every training photo needs one")
        if row.get("split", "train") not in ("train", "val"):
            raise SystemExit(f"{path}:{n}: split must be train or val")
        for k in ("image", "mask"):
            if row.get(k):
                row[k] = str((base / row[k]).resolve())
        rows.append(row)
    return rows


def _write_ppm(path: str, rgb: np.ndarray) -> None:
    h, w, _ = rgb.shape
    with open(path, "wb") as f:
        f.write(f"P6\n{w} {h}\n255\n".encode())
        f.write(np.ascontiguousarray(rgb, np.uint8).tobytes())


def _write_pgm(path: str, g: np.ndarray) -> None:
    h, w = g.shape
    with open(path, "wb") as f:
        f.write(f"P5\n{w} {h}\n255\n".encode())
        f.write(np.ascontiguousarray(g, np.uint8).tobytes())


def _read_ppm(path: str) -> np.ndarray:
    data = Path(path).read_bytes()
    parts, i = [], 0
    while len(parts) < 4:
        while data[i : i + 1].isspace():
            i += 1
        j = i
        while not data[j : j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(data[i + 1 : i + 1 + w * h * 3], np.uint8).reshape(h, w, 3)


def fit(img: Image.Image, size: int) -> Image.Image:
    """Long side to [size], both sides a multiple of 8 (the network's stride)."""
    w, h = img.size
    s = size / max(w, h)
    return img.resize((max(8, round(w * s / 8) * 8), max(8, round(h * s / 8) * 8)), Image.LANCZOS)


def teacher(binary: str, photo: str, mask: str | None, faces, contours, params: list[float], seed: int, out: str) -> None:
    style = int(round(params[0]))
    env = dict(os.environ)
    # TOON="background lines shades vivid detail eyes skin glow strength" (background and shades whole numbers).
    p = [f"{v:.4f}" for v in params]
    env["TOON"] = " ".join([str(int(round(params[1]))), p[2], str(int(round(params[3]))), *p[4:9], "1"])
    env["TOON_FINE"] = " ".join([*p[10:17], str(int(round(params[17])))])
    if faces:
        env["ART_FACE_STRIDE"] = "12"
        env["ART_FACES"] = " ".join(f"{v:.5f}" for f in faces for v in f)
    if contours:
        env["TOON_CONTOURS"] = " ".join(f"{v:.5f}" for c in contours for v in c)
    args = [binary, photo, out, str(style), "1", mask or "-"]
    if seed:
        args += ["0", "0", "0", "0", str(seed)]
    r = subprocess.run(args, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"teacher failed on {photo}: {r.stderr.strip()}")


def make(args: argparse.Namespace) -> None:
    rows = read_manifest(args.manifest)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    index = []
    with tempfile.TemporaryDirectory() as tmp:
        for row in rows:
            src = Image.open(row["image"]).convert("RGB")
            msrc = Image.open(row["mask"]).convert("L") if row.get("mask") else None
            key = hashlib.sha256(Path(row["image"]).read_bytes()).hexdigest()[:16]
            for size in args.sizes:
                img = fit(src, size)
                rgb = np.asarray(img)
                m = np.asarray(msrc.resize(img.size, Image.BILINEAR)) if msrc else np.zeros(rgb.shape[:2], np.uint8)
                stem = f"{key}_{size}"
                np.savez_compressed(out / f"{stem}.npz", image=rgb, mask=m, has_mask=msrc is not None)
                ppm, pgm, res = f"{tmp}/p.ppm", f"{tmp}/m.pgm", f"{tmp}/o.ppm"
                _write_ppm(ppm, rgb)
                if msrc:
                    _write_pgm(pgm, m)
                for k in range(args.per_photo):
                    params = sample_params(rng)
                    teacher(args.teacher, ppm, pgm if msrc else None, row.get("faces"), row.get("contours"), params, 0, res)
                    target = _read_ppm(res)
                    np.savez_compressed(out / f"{stem}_{k:03d}.npz", target=target, params=np.array(params, np.float32))
                    index.append({"photo": f"{stem}.npz", "pair": f"{stem}_{k:03d}.npz", "split": row.get("split", "train"),
                                  "licence": row["licence"]})
                print(f"{Path(row['image']).name} @ {size}: {args.per_photo} pairs", file=sys.stderr)
    (out / "index.json").write_text(json.dumps(index, indent=0))
    print(f"{len(index)} pairs in {out}", file=sys.stderr)


class Pairs:
    """Random aligned crops of (photo, mask, settings) -> teacher picture, as tensors in the model's ranges."""

    def __init__(self, root: str, split: str, crop: int, seed: int = 0) -> None:
        import torch  # noqa: F401  (only the dataset needs it)

        self.root = Path(root)
        self.items = [r for r in json.loads((self.root / "index.json").read_text()) if r["split"] == split]
        if not self.items:
            raise SystemExit(f"no {split} pairs in {root}")
        self.crop = crop
        self.rng = random.Random(seed)
        self._photos: dict[str, tuple[np.ndarray, np.ndarray, bool]] = {}

    def __len__(self) -> int:
        return len(self.items)

    def photo(self, name: str):
        if name not in self._photos:
            z = np.load(self.root / name)
            self._photos[name] = (z["image"], z["mask"], bool(z["has_mask"]))
        return self._photos[name]

    def sample(self, i: int, crop: int | None = None):
        import torch

        r = self.items[i]
        img, mask, has_mask = self.photo(r["photo"])
        z = np.load(self.root / r["pair"])
        tgt, params = z["target"], z["params"].tolist()
        assert len(params) == PARAMS
        h, w = img.shape[:2]
        c = crop or self.crop
        if c and c < min(h, w):
            y, x = self.rng.randrange(0, (h - c) // 8 + 1) * 8, self.rng.randrange(0, (w - c) // 8 + 1) * 8
            img, mask, tgt = img[y : y + c, x : x + c], mask[y : y + c, x : x + c], tgt[y : y + c, x : x + c]
        t = lambda a: torch.from_numpy(np.ascontiguousarray(a)).float()  # noqa: E731
        return (t(img).permute(2, 0, 1) / 127.5 - 1, t(mask)[None] / 255, torch.tensor(cond_from_params(params, has_mask)),
                t(tgt).permute(2, 0, 1) / 127.5 - 1)

    def batch(self, n: int):
        import torch

        got = [self.sample(self.rng.randrange(len(self.items))) for _ in range(n)]
        return [torch.stack(x) for x in zip(*got)]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    mk = sub.add_parser("make", help="draw teacher pairs")
    mk.add_argument("--manifest", required=True)
    mk.add_argument("--teacher", required=True, help="native/engine/build/toon_demo (PP_ENGINE_BUILD_TOOLS=ON)")
    mk.add_argument("--out", required=True)
    mk.add_argument("--per-photo", type=int, default=40)
    mk.add_argument("--sizes", type=int, nargs="+", default=[512, 768])
    mk.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    if a.cmd == "make":
        make(a)


if __name__ == "__main__":
    main()
