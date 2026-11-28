"""The settings a ToonNet picture is drawn with, as the model reads them.

The app sends Toon 18 settings (StudioModule.renderToon, ToonInput.params):

    0 style 0..5        1 background 0..5   2 lines 0..1       3 shades 2..6
    4 vivid -1..1       5 detail 0..1       6 eyes 0..1        7 skin 0..1
    8 glow 0..1         9 strength 0..1     10 outline 0..1    11 backdrop 0..1
    12 brightness -1..1 13 warmth -1..1     14 texture 0..1    15 face detail 0..1
    16 hair detail 0..1 17 ink 0..3

The model reads them as a vector of COND floats, every one in [0, 1]:
style, background and ink one-hot, then the 14 sliders scaled to [0, 1], then
whether a person mask was given. Strength is not in it: the app blends the
picture over the photo by Strength, as it does for the classic engine.

ModelToonEngine.cond (Kotlin) must give exactly the same vector; the tests on
both sides check one fixed example (EXAMPLE_PARAMS, EXAMPLE_COND).
"""

from __future__ import annotations

import random

STYLES = ("anime", "manga", "comic", "cartoon", "webtoon", "retro")
BACKGROUNDS = ("keep", "soft", "sky", "sunset", "night", "plain")
INKS = ("style", "black", "brown", "colour")
PARAMS = 18
# (index in params, low, high) for the sliders, in the order the model reads them.
SLIDERS = (
    (2, 0.0, 1.0),  # lines
    (3, 2.0, 6.0),  # shades
    (4, -1.0, 1.0),  # vivid
    (5, 0.0, 1.0),  # detail
    (6, 0.0, 1.0),  # eyes
    (7, 0.0, 1.0),  # skin
    (8, 0.0, 1.0),  # glow
    (10, 0.0, 1.0),  # outline
    (11, 0.0, 1.0),  # backdrop
    (12, -1.0, 1.0),  # brightness
    (13, -1.0, 1.0),  # warmth
    (14, 0.0, 1.0),  # texture
    (15, 0.0, 1.0),  # face detail
    (16, 0.0, 1.0),  # hair detail
)
COND = len(STYLES) + len(BACKGROUNDS) + len(INKS) + len(SLIDERS) + 1  # 31

# Each style's own start (src/services/toon/catalog.ts STYLES[].start):
# lines, shades, vivid, detail, eyes, skin, glow.
STYLE_START = (
    (0.5, 3, 0.3, 0.5, 0.45, 0.7, 0.35),
    (0.6, 3, 0.0, 0.55, 0.4, 0.7, 0.0),
    (0.75, 4, 0.5, 0.45, 0.15, 0.5, 0.0),
    (0.8, 2, 0.5, 0.25, 0.5, 0.8, 0.2),
    (0.35, 4, 0.1, 0.6, 0.35, 0.8, 0.55),
    (0.55, 2, -0.2, 0.45, 0.4, 0.6, 0.15),
)
# FINE_DEFAULTS: outline, backdrop, brightness, warmth, texture, face, hair, ink.
FINE_START = (0.5, 0.5, 0.0, 0.0, 0.5, 0.5, 0.5, 0)


def _clamp(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else hi if v > hi else v


def _one_hot(i: float, n: int) -> list[float]:
    k = int(_clamp(round(i), 0, n - 1))
    return [1.0 if j == k else 0.0 for j in range(n)]


def cond_from_params(params: list[float], has_mask: bool) -> list[float]:
    """The model's COND vector for the app's 18 settings."""
    if len(params) != PARAMS:
        raise ValueError(f"toon settings: {PARAMS} values")
    out = _one_hot(params[0], len(STYLES)) + _one_hot(params[1], len(BACKGROUNDS)) + _one_hot(params[17], len(INKS))
    for i, lo, hi in SLIDERS:
        out.append((_clamp(params[i], lo, hi) - lo) / (hi - lo))
    out.append(1.0 if has_mask else 0.0)
    return out


def start_params(style: int, background: int = 0) -> list[float]:
    """The settings a style starts at (what the app's style card uses)."""
    lines, shades, vivid, detail, eyes, skin, glow = STYLE_START[style]
    outline, backdrop, bright, warm, texture, face, hair, ink = FINE_START
    return [style, background, lines, shades, vivid, detail, eyes, skin, glow, 1.0,
            outline, backdrop, bright, warm, texture, face, hair, ink]


def sample_params(rng: random.Random) -> list[float]:
    """Settings for one teacher picture: mostly near a style's start, as people use it, sometimes anywhere."""
    style = rng.randrange(len(STYLES))
    background = 0 if rng.random() < 0.45 else rng.randrange(len(BACKGROUNDS))
    p = start_params(style, background)
    wide = rng.random() < 0.25
    for i, lo, hi in SLIDERS:
        if i == 3:
            p[i] = float(rng.randint(2, 6)) if wide or rng.random() < 0.3 else p[i]
            continue
        if wide:
            p[i] = rng.uniform(lo, hi)
        elif rng.random() < 0.5:
            p[i] = _clamp(p[i] + rng.gauss(0.0, 0.18 * (hi - lo)), lo, hi)
    p[17] = 0 if rng.random() < 0.6 else rng.randrange(len(INKS))
    return p


# The fixed example both sides test (Kotlin: ModelToonEngineTest).
EXAMPLE_PARAMS = [2, 3, 0.75, 4, 0.5, 0.45, 0.15, 0.5, 0, 0.8, 0.25, 1, -0.5, 0.5, 0.5, 0.6, 0.4, 1]
EXAMPLE_COND = (
    [0, 0, 1, 0, 0, 0] + [0, 0, 0, 1, 0, 0] + [0, 1, 0, 0]
    + [0.75, 0.5, 0.75, 0.45, 0.15, 0.5, 0, 0.25, 1, 0.25, 0.75, 0.5, 0.6, 0.4] + [1]
)
