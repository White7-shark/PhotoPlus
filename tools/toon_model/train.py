"""Train ToonNet.

Stage 1, distil (paired): learn every style and control from the app's classic
engine (data.py pairs). This alone gives a model that draws like the app, one
network for all settings, and starts stage 2 from the app's own look.

    python3 train.py --pairs pairs/ --out runs/distil --steps 60000 --crop 256 --batch 16

Stage 2, style (unpaired, optional): sharpen the look towards hand-drawn art
the app has the rights to (commissioned or licensed illustrations; see
README.md). A patch critic compares ToonNet's pictures with the art; the
teacher stays as an anchor so faces stay the person's own.

    python3 train.py --pairs pairs/ --style-dir art/anime --style 0 --resume runs/distil/last.pt \
        --out runs/anime --steps 20000

Every checkpoint keeps an exponential moving average of the weights (the one
export.py uses) and the data's licences, so a shipped model can always say
what it learned from.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import random
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

from data import Pairs
from model import PatchCritic, ToonNet, count

SOBEL = torch.tensor([[1.0, 0.0, -1.0], [2.0, 0.0, -2.0], [1.0, 0.0, -1.0]])


def luma(x: torch.Tensor) -> torch.Tensor:
    return (0.299 * x[:, 0:1] + 0.587 * x[:, 1:2] + 0.114 * x[:, 2:3])


def edges(x: torch.Tensor) -> torch.Tensor:
    k = torch.stack([SOBEL, SOBEL.t()])[:, None].to(x)
    return F.conv2d(luma(x), k, padding=1)


def yuv_chroma(x: torch.Tensor) -> torch.Tensor:
    r, g, b = x[:, 0:1], x[:, 1:2], x[:, 2:3]
    return torch.cat([-0.147 * r - 0.289 * g + 0.436 * b, 0.615 * r - 0.515 * g - 0.100 * b], 1)


def distil_loss(out: torch.Tensor, tgt: torch.Tensor) -> tuple[torch.Tensor, dict]:
    """Colour, line art and the large shapes all match the teacher."""
    l1 = (out - tgt).abs().mean()
    e = (edges(out) - edges(tgt)).abs().mean()
    ms = sum((F.avg_pool2d(out, s) - F.avg_pool2d(tgt, s)).abs().mean() for s in (2, 4))
    total = l1 + 0.5 * e + 0.5 * ms
    return total, {"l1": l1.item(), "edge": e.item()}


def smooth_edges(art: torch.Tensor) -> torch.Tensor:
    """Art with its line edges blurred: shown to the critic as 'not art', so ToonNet learns crisp lines (AnimeGAN's idea)."""
    e = edges(art).abs().sum(1, keepdim=True)
    near = F.max_pool2d((e > 0.4).float(), 5, 1, 2)
    return art * (1 - near) + F.avg_pool2d(art, 5, 1, 2) * near


class Art:
    """Random crops of the style images (unpaired)."""

    def __init__(self, root: str, crop: int, seed: int) -> None:
        exts = {".jpg", ".jpeg", ".png", ".webp"}
        self.files = sorted(p for p in Path(root).rglob("*") if p.suffix.lower() in exts)
        licence = Path(root) / "LICENCE.txt"
        if not self.files:
            raise SystemExit(f"no style images in {root}")
        if not licence.exists():
            raise SystemExit(f"{licence} is missing: say who made the art and why Photo Plus may train on it")
        self.licence = licence.read_text().strip()
        self.crop, self.rng = crop, random.Random(seed)

    def batch(self, n: int) -> torch.Tensor:
        out = []
        for _ in range(n):
            im = Image.open(self.rng.choice(self.files)).convert("RGB")
            s = self.crop / min(im.size) * self.rng.uniform(1.0, 1.6)
            im = im.resize((max(self.crop, round(im.width * s)), max(self.crop, round(im.height * s))), Image.LANCZOS)
            x, y = self.rng.randrange(im.width - self.crop + 1), self.rng.randrange(im.height - self.crop + 1)
            a = np.asarray(im.crop((x, y, x + self.crop, y + self.crop)), np.float32) / 127.5 - 1
            out.append(torch.from_numpy(a).permute(2, 0, 1))
        return torch.stack(out)


@torch.no_grad()
def ema_update(ema: torch.nn.Module, net: torch.nn.Module, decay: float) -> None:
    for e, p in zip(ema.parameters(), net.parameters()):
        e.lerp_(p, 1 - decay)


@torch.no_grad()
def validate(net: torch.nn.Module, val: Pairs | None, n: int = 12) -> dict:
    if val is None:
        return {}
    net.eval()
    psnr, l1 = [], []
    for i in range(min(n, len(val))):
        img, mask, cond, tgt = (t[None] for t in val.sample(i, crop=0))
        out = net(img, mask, cond)
        mse = ((out - tgt) ** 2).mean() / 4  # [-1, 1] -> [0, 1]
        psnr.append(float(10 * torch.log10(1 / mse)))
        l1.append(float((out - tgt).abs().mean() * 127.5))
    net.train()
    return {"val_psnr": round(sum(psnr) / len(psnr), 2), "val_l1_8bit": round(sum(l1) / len(l1), 2)}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--steps", type=int, default=60000)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--crop", type=int, default=256)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--width", type=int, default=32)
    ap.add_argument("--blocks", type=int, default=8)
    ap.add_argument("--ema", type=float, default=0.999)
    ap.add_argument("--resume", help="a checkpoint to start from (stage 2 starts from stage 1)")
    ap.add_argument("--style-dir", help="stage 2: licensed art for one style (with LICENCE.txt)")
    ap.add_argument("--style", type=int, help="stage 2: which style (0..5) the art is for")
    ap.add_argument("--adv", type=float, default=0.3, help="stage 2: weight of the critic")
    ap.add_argument("--every", type=int, default=500, help="validate and save every this many steps")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    a = ap.parse_args()
    torch.manual_seed(a.seed)
    dev = torch.device(a.device)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    train = Pairs(a.pairs, "train", a.crop, a.seed)
    try:
        val = Pairs(a.pairs, "val", 0, a.seed + 1)
    except SystemExit:
        val = None
    net = ToonNet(a.width, a.blocks).to(dev)
    start = 0
    if a.resume:
        ck = torch.load(a.resume, map_location=dev, weights_only=True)
        net.load_state_dict(ck["ema"])
        start = 0 if a.style_dir else int(ck.get("step", 0))
    ema = copy.deepcopy(net).eval().requires_grad_(False)
    opt = torch.optim.AdamW(net.parameters(), lr=a.lr, betas=(0.5, 0.99) if a.style_dir else (0.9, 0.99), weight_decay=1e-4)
    art = critic = copt = None
    if a.style_dir:
        if a.style is None:
            raise SystemExit("--style is needed with --style-dir")
        art = Art(a.style_dir, a.crop, a.seed)
        critic = PatchCritic().to(dev)
        copt = torch.optim.AdamW(critic.parameters(), lr=a.lr * 2, betas=(0.5, 0.99))
    licences = sorted({r["licence"] for r in train.items + (val.items if val else [])})
    print(json.dumps({"params": count(net), "train_pairs": len(train), "val_pairs": len(val) if val else 0,
                      "device": str(dev), "stage": "style" if art else "distil"}))

    total = start + a.steps
    t0, log = time.time(), []
    for step in range(start + 1, total + 1):
        lr = a.lr * 0.5 * (1 + math.cos(math.pi * (step - start) / a.steps))
        for g in opt.param_groups:
            g["lr"] = lr
        img, mask, cond, tgt = (t.to(dev) for t in train.batch(a.batch))
        if art is not None:
            cond[:, :6] = 0  # stage 2 trains one style: its pictures are all drawn in that style
            cond[:, a.style] = 1
        o = net(img, mask, cond)
        loss, parts = distil_loss(o, tgt)
        if art is not None:
            real = art.batch(a.batch).to(dev)
            # The critic: art is real; ToonNet's pictures and edge-smoothed art are not.
            d_real, d_fake, d_soft = critic(real), critic(o.detach()), critic(smooth_edges(real))
            d_loss = ((d_real - 1) ** 2).mean() + 0.5 * (d_fake**2).mean() + 0.5 * (d_soft**2).mean()
            copt.zero_grad(set_to_none=True)
            d_loss.backward()
            copt.step()
            g_adv = ((critic(o) - 1) ** 2).mean()
            colour = (yuv_chroma(o) - yuv_chroma(tgt)).abs().mean()
            loss = loss + a.adv * g_adv + 0.5 * colour
            parts.update(adv=g_adv.item(), critic=d_loss.item())
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        opt.step()
        ema_update(ema, net, a.ema if step - start > 200 else 0.9)
        if step % 50 == 0:
            print(json.dumps({"step": step, "loss": round(loss.item(), 4), **{k: round(v, 4) for k, v in parts.items()},
                              "lr": round(lr, 6), "s_per_step": round((time.time() - t0) / (step - start), 3)}), flush=True)
        if step % a.every == 0 or step == total:
            v = validate(ema, val)
            log.append({"step": step, **v})
            print(json.dumps({"step": step, **v}), flush=True)
            torch.save({"step": step, "net": net.state_dict(), "ema": ema.state_dict(),
                        "config": {"width": a.width, "blocks": a.blocks}, "licences": licences
                        + ([art.licence] if art else []), "val": log}, out / "last.pt")
    (out / "val.json").write_text(json.dumps(log, indent=1))


if __name__ == "__main__":
    main()
