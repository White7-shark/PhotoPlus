"""Export a trained ToonNet for the app.

    python3 export.py --ckpt runs/anime/last.pt --out build/ --size 512

Writes:
- toon_photoplus.onnx: the network with the app's layout. Three inputs:
  image [1, H, W, 3] in [-1, 1] (NHWC, as InferenceSession prepares it),
  mask [1, H, W, 1] in [0, 1], cond [1, 31]. Output: [1, H, W, 3] in [-1, 1].
- toon_photoplus.json: its model card (size, sha256, data licences, val scores).
- toon_photoplus.tflite, when litert-torch is installed (the converter for
  the app's LiteRT runtime): `pip install litert-torch`, then run again.
  It needs about 3 GB of disk; convert on a workstation or CI.

The export is checked: onnxruntime must give the same picture as PyTorch.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import torch
from torch import nn

from cond import COND
from model import ToonNet, count


class AppLayout(nn.Module):
    """ToonNet with NHWC tensors, as the app feeds LiteRT."""

    def __init__(self, net: ToonNet) -> None:
        super().__init__()
        self.net = net

    def forward(self, image: torch.Tensor, mask: torch.Tensor, cond: torch.Tensor) -> torch.Tensor:
        out = self.net(image.permute(0, 3, 1, 2), mask.permute(0, 3, 1, 2), cond)
        return out.permute(0, 2, 3, 1)


def load(ckpt: str) -> tuple[ToonNet, dict]:
    ck = torch.load(ckpt, map_location="cpu", weights_only=True)
    cfg = ck.get("config", {})
    net = ToonNet(cfg.get("width", 32), cfg.get("blocks", 8))
    net.load_state_dict(ck["ema"])
    return net.eval(), ck


def example(size: int, seed: int = 0) -> tuple[torch.Tensor, ...]:
    g = torch.Generator().manual_seed(seed)
    return (torch.rand(1, size, size, 3, generator=g) * 2 - 1, torch.rand(1, size, size, 1, generator=g),
            torch.rand(1, COND, generator=g))


def to_onnx(model: nn.Module, size: int, path: Path) -> None:
    prog = torch.onnx.export(model, example(size), dynamo=True, input_names=["image", "mask", "cond"],
                             output_names=["toon"], opset_version=18)
    prog.save(str(path))


def check_onnx(model: nn.Module, path: Path, size: int) -> float:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    worst = 0.0
    for seed in range(3):
        x = example(size, seed)
        with torch.no_grad():
            want = model(*x).numpy()
        got = sess.run(None, {"image": x[0].numpy(), "mask": x[1].numpy(), "cond": x[2].numpy()})[0]
        worst = max(worst, float(np.abs(got - want).max()))
    return worst


def to_litert(model: nn.Module, size: int, path: Path) -> bool:
    try:
        import litert_torch  # type: ignore[import-not-found]
    except ImportError:
        return False
    edge = litert_torch.convert(model, example(size))
    edge.export(str(path))
    return True


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=512)
    a = ap.parse_args()
    if a.size % 8:
        raise SystemExit("--size must be a multiple of 8")
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    net, ck = load(a.ckpt)
    model = AppLayout(net).eval()
    onnx_path = out / "toon_photoplus.onnx"
    to_onnx(model, a.size, onnx_path)
    err = check_onnx(model, onnx_path, a.size)
    if err > 2e-3:
        raise SystemExit(f"onnx export differs from PyTorch by {err:.4f}")
    tfl = out / "toon_photoplus.tflite"
    litert = to_litert(model, a.size, tfl)
    shipped = tfl if litert else onnx_path
    card = {
        "name": "ToonNet (Photo Plus)",
        "file": shipped.name,
        "sha256": hashlib.sha256(shipped.read_bytes()).hexdigest(),
        "bytes": shipped.stat().st_size,
        "params": count(net),
        "input": {"image": [1, a.size, a.size, 3], "mask": [1, a.size, a.size, 1], "cond": [1, COND]},
        "range": "image and output in [-1, 1]; mask and cond in [0, 1]",
        "onnx_max_abs_diff": err,
        "trained_steps": ck.get("step"),
        "data_licences": ck.get("licences", []),
        "val": ck.get("val", [])[-1:] or None,
    }
    (out / "toon_photoplus.json").write_text(json.dumps(card, indent=1))
    print(json.dumps({k: card[k] for k in ("file", "bytes", "params", "onnx_max_abs_diff")}))
    if not litert:
        print("litert-torch is not installed: wrote ONNX only (see the docstring for the LiteRT step)")


if __name__ == "__main__":
    main()
