"""ToonNet: Photo Plus's own photo-to-toon generator.

Made for the app, not adapted from a published model:
- It reads the photo, the person's mask (the app already has it from ML Kit)
  and the settings vector (cond.py). One network draws every style,
  background, ink and slider the app has, instead of one model per look.
- Where it is in the picture (two coordinate channels) lets it draw skies and
  gradients behind the person.
- A U-Net: three downsamplings to 1/8, eight residual blocks there, each
  steered by the settings (FiLM), then back up with the encoder's detail. The
  skips keep the person's own features (identity), the app's first rule.
- Light enough for a phone: about 1.1 M weights, depthwise-separable blocks,
  resize-convolutions (no checkerboard), LeakyReLU and tanh only. Every
  operator converts to LiteRT and runs on its GPU delegate.

Inputs, NCHW here (export.py gives the app NHWC):
  image [N, 3, H, W] in [-1, 1], mask [N, 1, H, W] in [0, 1], cond [N, COND] in [0, 1].
Output: [N, 3, H, W] in [-1, 1]. H and W must be multiples of 8.
"""

from __future__ import annotations

import torch
import torch.nn.functional as F
from torch import nn

from cond import COND


def act() -> nn.Module:
    return nn.LeakyReLU(0.2)


class Down(nn.Module):
    def __init__(self, cin: int, cout: int) -> None:
        super().__init__()
        self.body = nn.Sequential(nn.Conv2d(cin, cout, 3, 2, 1), act(), nn.Conv2d(cout, cout, 3, 1, 1), act())

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.body(x)


class Block(nn.Module):
    """Depthwise-separable residual block whose features are scaled and shifted by the settings."""

    def __init__(self, ch: int, hidden: int) -> None:
        super().__init__()
        self.dw = nn.Conv2d(ch, ch, 3, 1, 1, groups=ch)
        self.pw1 = nn.Conv2d(ch, hidden, 1)
        self.pw2 = nn.Conv2d(hidden, ch, 1)
        self.a = act()
        nn.init.zeros_(self.pw2.weight)  # starts as the identity: stable to train with no normalisation
        nn.init.zeros_(self.pw2.bias)

    def forward(self, x: torch.Tensor, gamma: torch.Tensor, beta: torch.Tensor) -> torch.Tensor:
        h = self.dw(x) * (1 + gamma) + beta
        return x + self.pw2(self.a(self.pw1(self.a(h))))


class Up(nn.Module):
    def __init__(self, cin: int, skip: int, cout: int) -> None:
        super().__init__()
        self.mix = nn.Conv2d(cin + skip, cout, 1)
        self.dw = nn.Conv2d(cout, cout, 3, 1, 1, groups=cout)
        self.pw = nn.Conv2d(cout, cout, 1)
        self.a = act()

    def forward(self, x: torch.Tensor, skip: torch.Tensor) -> torch.Tensor:
        x = F.interpolate(x, size=skip.shape[-2:], mode="bilinear", align_corners=False)
        x = self.a(self.mix(torch.cat([x, skip], 1)))
        return self.a(self.pw(self.dw(x))) + x


class ToonNet(nn.Module):
    def __init__(self, width: int = 32, blocks: int = 8) -> None:
        super().__init__()
        w1, w2, w3, w4 = width, width * 2, width * 3, width * 4
        self.blocks_n = blocks
        self.w4 = w4
        self.stem = nn.Sequential(nn.Conv2d(3 + 1 + 2, w1, 3, 1, 1), act(), nn.Conv2d(w1, w1, 3, 1, 1), act())
        self.d1, self.d2, self.d3 = Down(w1, w2), Down(w2, w3), Down(w3, w4)
        self.film = nn.Sequential(nn.Linear(COND, 128), act(), nn.Linear(128, blocks * 2 * w4))
        nn.init.zeros_(self.film[-1].weight)
        nn.init.zeros_(self.film[-1].bias)
        self.body = nn.ModuleList(Block(w4, w4 * 2) for _ in range(blocks))
        self.u3, self.u2, self.u1 = Up(w4, w3, w3), Up(w3, w2, w2), Up(w2, w1, w1)
        # The last layer also sees the photo: colour where the style keeps it.
        self.head = nn.Sequential(nn.Conv2d(w1 + 3, w1, 3, 1, 1), act(), nn.Conv2d(w1, 3, 3, 1, 1))

    @staticmethod
    def coords(x: torch.Tensor) -> torch.Tensor:
        n, _, h, w = x.shape
        ys = torch.linspace(-1.0, 1.0, h, dtype=x.dtype, device=x.device).view(1, 1, h, 1).expand(n, 1, h, w)
        xs = torch.linspace(-1.0, 1.0, w, dtype=x.dtype, device=x.device).view(1, 1, 1, w).expand(n, 1, h, w)
        return torch.cat([ys, xs], 1)

    def forward(self, image: torch.Tensor, mask: torch.Tensor, cond: torch.Tensor) -> torch.Tensor:
        s0 = self.stem(torch.cat([image, mask, self.coords(image)], 1))
        s1 = self.d1(s0)
        s2 = self.d2(s1)
        x = self.d3(s2)
        f = self.film(cond).view(-1, self.blocks_n, 2, self.w4, 1, 1)
        for i, blk in enumerate(self.body):
            x = blk(x, f[:, i, 0], f[:, i, 1])
        x = self.u3(x, s2)
        x = self.u2(x, s1)
        x = self.u1(x, s0)
        return torch.tanh(self.head(torch.cat([x, image], 1)))


class PatchCritic(nn.Module):
    """Stage 2's discriminator: is this 70-pixel patch hand-drawn art or ToonNet's? Spectral norm keeps it fair."""

    def __init__(self, width: int = 32) -> None:
        super().__init__()
        sn = nn.utils.parametrizations.spectral_norm
        layers: list[nn.Module] = []
        c = 3
        for i, cout in enumerate((width, width * 2, width * 4, width * 8)):
            layers += [sn(nn.Conv2d(c, cout, 4, 2 if i < 3 else 1, 1)), act()]
            c = cout
        layers.append(sn(nn.Conv2d(c, 1, 4, 1, 1)))
        self.net = nn.Sequential(*layers)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


def count(m: nn.Module) -> int:
    return sum(p.numel() for p in m.parameters())
