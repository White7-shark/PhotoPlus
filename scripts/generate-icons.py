#!/usr/bin/env python3
"""
Generates every Photo Plus brand asset from the master logo artwork
(assets/brand/source/photoplus-logo.png). Requires Pillow (pip install pillow).

Usage:  python3 scripts/generate-icons.py

Outputs
  assets/brand/photoplus-logo.png            full logo, transparent corners (1024px)
  assets/brand/photoplus-emblem.png          camera emblem tile (1024px)
  assets/brand/play-store-icon.png           512px full-bleed icon for Google Play
  src/assets/brand/logo-full{,@2x,@3x}.png   in-app full logo (onboarding, splash)
  src/assets/brand/logo-emblem{,@2x,@3x}.png in-app mark (headers, small places)
  android/.../mipmap-*/ic_launcher_foreground.png  adaptive icon foreground
  android/.../mipmap-*/ic_launcher_monochrome.png  themed (Material You) icon

Launcher icons use the camera emblem rather than the full logo: Android masks
launcher icons to a ~66dp-diameter safe zone and draws the app name under
them, so the wordmark and tagline would be cropped or unreadably small.
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets/brand/source/photoplus-logo.png"
RES = ROOT / "android/app/src/main/res"

# Geometry of the rounded square inside the 1254px source (measured).
SQUARE = (47, 46, 1207, 1207)  # left, top, right, bottom (exclusive)
RADIUS = 250
# Camera + swoosh + sparkle emblem, in source pixels.
EMBLEM = (232, 124, 1128, 742)
# Dark navy behind the emblem, used to extend crops seamlessly.
BG = (5, 4, 30)

DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}


def rounded_mask(size: tuple[int, int], radius: float, scale: int = 4) -> Image.Image:
    """Anti-aliased rounded-rectangle mask (supersampled)."""
    w, h = size
    big = Image.new("L", (w * scale, h * scale), 0)
    ImageDraw.Draw(big).rounded_rectangle((0, 0, w * scale - 1, h * scale - 1), radius=radius * scale, fill=255)
    return big.resize((w, h), Image.LANCZOS)


def full_logo(src: Image.Image) -> Image.Image:
    sq = src.crop(SQUARE).convert("RGBA")
    sq.putalpha(rounded_mask(sq.size, RADIUS))
    return sq


def feathered_emblem(src: Image.Image, canvas: int, content: float) -> Image.Image:
    """Emblem centred on a BG square; crop edges feather into BG so no seam shows.

    `content` is the fraction of the canvas the emblem's longest side occupies.
    """
    emb = src.crop(EMBLEM).convert("RGB")
    ew, eh = emb.size
    scale = canvas * content / max(ew, eh)
    emb = emb.resize((round(ew * scale), round(eh * scale)), Image.LANCZOS)
    feather = max(2, round(min(emb.size) * 0.08))
    mask = Image.new("L", emb.size, 0)
    ImageDraw.Draw(mask).rectangle((feather, feather, emb.width - feather, emb.height - feather), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(feather / 2))
    out = Image.new("RGB", (canvas, canvas), BG)
    out.paste(emb, ((canvas - emb.width) // 2, (canvas - emb.height) // 2), mask)
    return out


def monochrome(emblem_rgb: Image.Image) -> Image.Image:
    """White silhouette for Android 13+ themed icons (alpha = brightness)."""
    lum = emblem_rgb.convert("L")
    bg_l = round(0.299 * BG[0] + 0.587 * BG[1] + 0.114 * BG[2])
    alpha = lum.point(lambda v: 0 if v <= bg_l + 18 else min(255, round((v - bg_l - 18) * 2.2)))
    out = Image.new("RGBA", emblem_rgb.size, (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


def save(img: Image.Image, path: Path, size: int | tuple[int, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    dims = (size, size) if isinstance(size, int) else size
    img.resize(dims, Image.LANCZOS).save(path, optimize=True)


def main() -> None:
    src = Image.open(SOURCE).convert("RGB")

    logo = full_logo(src)
    save(logo, ROOT / "assets/brand/photoplus-logo.png", 1024)

    tile = feathered_emblem(src, 1024, 0.86).convert("RGBA")
    tile.putalpha(rounded_mask(tile.size, 1024 * 0.225))
    save(tile, ROOT / "assets/brand/photoplus-emblem.png", 1024)

    # Google Play: 512x512 full-bleed square (Play applies its own mask).
    play = src.crop(SQUARE).resize((512, 512), Image.LANCZOS)
    play.save(ROOT / "assets/brand/play-store-icon.png", optimize=True)

    # In-app assets (React Native picks @2x/@3x by screen density).
    for suffix, factor in (("", 1), ("@2x", 2), ("@3x", 3)):
        save(logo, ROOT / f"src/assets/brand/logo-full{suffix}.png", 240 * factor)
        save(tile, ROOT / f"src/assets/brand/logo-emblem{suffix}.png", 96 * factor)

    # Adaptive icon (108dp canvas; emblem kept inside the 66dp safe circle).
    fg_master = feathered_emblem(src, 1080, 0.51)
    mono_master = monochrome(fg_master)
    for name, factor in DENSITIES.items():
        px = round(108 * factor)
        save(fg_master, RES / f"mipmap-{name}/ic_launcher_foreground.png", px)
        save(mono_master, RES / f"mipmap-{name}/ic_launcher_monochrome.png", px)

    # Sanity check: emblem diagonal must fit the safe zone (66/108 of canvas).
    ew, eh = EMBLEM[2] - EMBLEM[0], EMBLEM[3] - EMBLEM[1]
    diag = math.hypot(ew, eh) * (1080 * 0.51 / max(ew, eh)) / 1080
    assert diag <= 66 / 108 + 0.03, f"emblem exceeds adaptive safe zone ({diag:.3f})"
    print(f"Brand assets generated (emblem diagonal {diag:.3f} of canvas; safe zone {66 / 108:.3f}).")


if __name__ == "__main__":
    main()
