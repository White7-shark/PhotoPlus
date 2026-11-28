"""How close ToonNet is to the app's classic engine on photos it never saw.

    python3 evaluate.py --ckpt runs/distil/last.pt --pairs pairs/ --out eval/

For each held-out ("val") pair: PSNR and mean error against the teacher,
whether its line art lands where the teacher's does (F1 of dark-line pixels),
and how much of the person's skin hue moved from the photo (skin is never to
be pushed orange). Writes eval.json and sheet.jpg (photo | teacher | ToonNet
for each style at its own start), and times one 512 x 512 picture on this CPU.
Use the sheet with the people in the photos' consent only; never commit it.
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw

import cond
from data import Pairs
from export import load


def to_u8(t: torch.Tensor) -> np.ndarray:
    return ((t.clamp(-1, 1) + 1) * 127.5).round().byte().permute(1, 2, 0).numpy()


def lines(rgb: np.ndarray) -> np.ndarray:
    """Dark, thin strokes: luminance well below its 7 x 7 neighbourhood."""
    y = rgb.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    pad = np.pad(y, 3, mode="edge")
    win = np.lib.stride_tricks.sliding_window_view(pad, (7, 7)).mean((-1, -2))
    return (win - y) > 18


def f1(a: np.ndarray, b: np.ndarray) -> float:
    tp = float((a & b).sum())
    return 2 * tp / max(1.0, float(a.sum() + b.sum()))


def hue_shift(photo: np.ndarray, out: np.ndarray, mask: np.ndarray) -> float:
    """Mean change of hue angle (degrees) inside the person, where colour is clear."""
    def hue(a: np.ndarray) -> np.ndarray:
        a = a.astype(np.float32) / 255
        r, g, b = a[..., 0], a[..., 1], a[..., 2]
        return np.degrees(np.arctan2(np.sqrt(3) * (g - b), 2 * r - g - b))

    sat = photo.max(-1).astype(np.float32) - photo.min(-1)
    keep = (mask > 127) & (sat > 25)
    if keep.sum() < 50:
        return 0.0
    d = np.abs(hue(photo) - hue(out))[keep]
    return float(np.minimum(d, 360 - d).mean())


@torch.no_grad()
def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--teacher", help="toon_demo, to draw the sheet's teacher row at each style's start")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    net, ck = load(a.ckpt)
    val = Pairs(a.pairs, "val", 0)
    rows = []
    for i, item in enumerate(val.items):
        img, mask, c, tgt = val.sample(i, crop=0)
        o = net(img[None], mask[None], c[None])[0]
        mse = float(((o - tgt) ** 2).mean() / 4)
        ou, tu = to_u8(o), to_u8(tgt)
        pu, mu = to_u8(img), (mask[0] * 255).byte().numpy()
        rows.append({"pair": item["pair"], "style": cond.STYLES[int(np.argmax(c[:6]))], "psnr": 10 * np.log10(1 / mse),
                     "l1_8bit": float(np.abs(ou.astype(int) - tu).mean()), "line_f1": f1(lines(ou), lines(tu)),
                     "skin_hue_shift": hue_shift(pu, ou, mu), "teacher_skin_hue_shift": hue_shift(pu, tu, mu)})
    summary = {k: round(float(np.mean([r[k] for r in rows])), 3) for k in rows[0] if k not in ("pair", "style")}
    per_style = {s: round(float(np.mean([r["psnr"] for r in rows if r["style"] == s])), 2)
                 for s in cond.STYLES if any(r["style"] == s for r in rows)}

    # Time one picture at the app's size.
    x, m, c = torch.rand(1, 3, 512, 512) * 2 - 1, torch.rand(1, 1, 512, 512), torch.rand(1, cond.COND)
    net(x, m, c)
    t0 = time.time()
    for _ in range(3):
        net(x, m, c)
    ms = (time.time() - t0) / 3 * 1000

    # The sheet: per val photo, each style at its own start (teacher row drawn now if given).
    tiles = []
    photos = sorted({it["photo"] for it in val.items if it["photo"].endswith("_512.npz")}) or sorted({it["photo"] for it in val.items})
    for name in photos[:2]:
        rgb, msk, has = val.photo(name)
        img = torch.from_numpy(rgb).float().permute(2, 0, 1)[None] / 127.5 - 1
        mk = torch.from_numpy(msk).float()[None, None] / 255
        row_net, row_t = [], []
        for s in range(len(cond.STYLES)):
            p = cond.start_params(s)
            row_net.append(to_u8(net(img, mk, torch.tensor([cond.cond_from_params(p, has)]))[0]))
            if a.teacher:
                row_t.append(draw_teacher(a.teacher, rgb, msk if has else None, p))
        tiles.append((rgb, row_t, row_net))
    if tiles:
        h, w = tiles[0][0].shape[:2]
        sc = 0.5
        th, tw = int(h * sc), int(w * sc)
        lines_n = sum(1 + (1 if t[1] else 0) + 1 for t in tiles)
        sheet = Image.new("RGB", (tw * (len(cond.STYLES) + 1), (th + 18) * lines_n), (24, 24, 28))
        d = ImageDraw.Draw(sheet)
        y = 0
        for rgb, row_t, row_net in tiles:
            for label, row in (("photo", [rgb]), ("classic engine", row_t), ("ToonNet", row_net)):
                if not row:
                    continue
                d.text((4, y + 3), label, fill=(230, 230, 230))
                for k, im in enumerate(row):
                    x0 = 0 if label == "photo" else tw * (k + 1)
                    sheet.paste(Image.fromarray(im).resize((tw, th)), (x0, y + 18))
                    if label != "photo":
                        d.text((x0 + 4, y + 3), cond.STYLES[k], fill=(230, 230, 230))
                y += th + 18
        sheet.save(out / "sheet.jpg", quality=88)

    report = {"checkpoint_step": ck.get("step"), "val_pairs": len(rows), "mean": summary, "psnr_by_style": per_style,
              "cpu_ms_512": round(ms, 1)}
    (out / "eval.json").write_text(json.dumps({"report": report, "pairs": rows}, indent=1))
    print(json.dumps(report, indent=1))


def draw_teacher(binary: str, rgb: np.ndarray, mask: np.ndarray | None, params: list[float]) -> np.ndarray:
    import tempfile

    from data import _read_ppm, _write_pgm, _write_ppm, teacher

    with tempfile.TemporaryDirectory() as d:
        _write_ppm(f"{d}/p.ppm", rgb)
        if mask is not None:
            _write_pgm(f"{d}/m.pgm", mask)
        teacher(binary, f"{d}/p.ppm", f"{d}/m.pgm" if mask is not None else None, None, None, params, 0, f"{d}/o.ppm")
        return _read_ppm(f"{d}/o.ppm")


if __name__ == "__main__":
    main()
