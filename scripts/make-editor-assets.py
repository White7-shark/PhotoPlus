"""
Real engine output for the Phase 3 design preview: the free-plan watermark,
every frame design, props fitted to a face, and an edited photo.

On the phone the watermark is drawn with Android Canvas
(WatermarkRenderer.kt); this script mirrors that layout exactly (same fonts,
emblem, sizes, gold swoosh and light/dark ink rule) with Pillow, then the
engine composites it (pp_overlay), as on the phone. Frames and props are
rendered by the engine through edit_demo; the face comes from MediaPipe's
face landmarker standing in for ML Kit.

  pip install numpy pillow mediapipe
  python3 scripts/make-editor-assets.py KODAK_DIR EDIT_DEMO FACE_LANDMARKER.task OUT_DIR [PORTRAIT]

PORTRAIT (optional) is a head-and-shoulders photo with room above the head,
used for the hat and neckwear previews (kodim15 is cropped at the top of
the head and at the neck).
"""
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

kodak, demo, landmarker, out = (Path(a) for a in sys.argv[1:5])
portrait = Path(sys.argv[5]) if len(sys.argv) > 5 else None
out.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parent.parent
assets = root / "android/app/src/main/assets"


def ppm(name: str) -> Path:
    p = out / f"{name}.ppm"
    Image.open(kodak / f"{name}.png").convert("RGB").save(p)
    return p


def pam(img: Image.Image, path: Path) -> Path:
    img = img.convert("RGBA")
    with open(path, "wb") as f:
        f.write(f"P7\nWIDTH {img.width}\nHEIGHT {img.height}\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n".encode())
        f.write(img.tobytes())
    return path


def run(*args) -> dict:
    return json.loads(subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True).stdout.strip())


# ---- the free-plan signature watermark (mirrors WatermarkRenderer.kt) -------
FREE_HEIGHT = (0.035, 0.08)
FREE_OPACITY = (0.6, 1.0)


# Proportions in units of the wordmark's cap height C (see WatermarkRenderer.kt).
C_PER_H, CAMERA_W, CAMERA_ASPECT, TEXT_LEFT = 0.46, 2.28, 300 / 320, 2.4
SCRIPT_BASELINE, WORDMARK_BASELINE, SCRIPT_WIDTH, PAD = 0.77, 2.2, 1.05, 0.32
DEFAULT_SIZE, DEFAULT_OPACITY = 0.78, 0.85


def _quad(p0, p1, p2, n=48):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in np.linspace(0, 1, n)]


def _tinted(img: Image.Image, rgb) -> Image.Image:
    t = Image.new("RGBA", img.size, tuple(rgb) + (0,))
    t.putalpha(img.split()[3])
    return t


def _hgradient(w, h, x0, x1, stops):
    xs = np.clip((np.arange(w) - x0) / max(1e-6, x1 - x0), 0, 1)
    pos = [p for p, _ in stops]
    cols = np.stack([np.interp(xs, pos, [c[i] for _, c in stops]) for i in range(3)], -1)
    return np.broadcast_to(cols[None, :, :], (h, w, 3)).astype(np.uint8)


def _vgradient(w, h, y0, y1, c0, c1):
    ys = np.clip((np.arange(h) - y0) / max(1e-6, y1 - y0), 0, 1)[:, None]
    cols = np.array(c0) * (1 - ys) + np.array(c1) * ys
    return np.broadcast_to(cols[:, None, :], (h, w, 3)).astype(np.uint8)


def signature_mark(photo: Image.Image, size=DEFAULT_SIZE, opacity=DEFAULT_OPACITY):
    W, H = photo.size
    short = min(W, H)
    h = max(12.0, (FREE_HEIGHT[0] + (FREE_HEIGHT[1] - FREE_HEIGHT[0]) * size) * short)
    c = C_PER_H * h
    S = 4  # draw at 4x and downsample, like anti-aliased Canvas text
    cs = c * S
    wfont_path = str(assets / "fonts/Poppins-ExtraBoldItalic.ttf")
    probe = ImageFont.truetype(wfont_path, 1000)
    hb = probe.getbbox("H")
    wsize = 1000 * cs / (hb[3] - hb[1])
    wf = ImageFont.truetype(wfont_path, round(wsize))
    word_w = wf.getlength("PhotoPlus")
    sfont = str(assets / "fonts/PetitFormalScript-Regular.ttf")
    sprobe = ImageFont.truetype(sfont, 1000)
    line = "Created with"
    sf = ImageFont.truetype(sfont, round(1000 * SCRIPT_WIDTH * word_w / sprobe.getlength(line)))
    cam_w, text_left = CAMERA_W * cs, TEXT_LEFT * cs
    cam_h = cam_w * CAMERA_ASPECT
    content_w = text_left + SCRIPT_WIDTH * word_w + 0.1 * cs
    content_h = WORDMARK_BASELINE * cs + 0.95 * cs
    pad = PAD * cs
    sw, sh = math.ceil((content_w + 2 * pad) / S), math.ceil((content_h + 2 * pad) / S)
    SW, SH = sw * S, sh * S
    margin = round(0.035 * short)
    x, y = W - sw - margin, H - sh - margin
    region = np.asarray(photo.convert("L").crop((x, y, x + sw, y + sh)), dtype=np.float64) / 255
    dark = float(region.mean()) > 0.62
    ink = (31, 31, 36) if dark else (255, 255, 255)
    shadow = (255, 255, 255, 128) if dark else (0, 0, 0, 140)

    layer = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
    # Camera: tinted outline, then the coloured lens and swoosh.
    box = (round(pad), round(pad), round(pad + cam_w), round(pad + cam_h))
    size_ = (box[2] - box[0], box[3] - box[1])
    lines = Image.open(assets / "brand/mark-camera-lines.png").convert("RGBA").resize(size_, Image.LANCZOS)
    color = Image.open(assets / "brand/mark-camera-color.png").convert("RGBA").resize(size_, Image.LANCZOS)
    layer.alpha_composite(_tinted(lines, ink), box[:2])
    layer.alpha_composite(color, box[:2])
    tx = pad + text_left
    # "Created with": slanted (skew -0.1) and slightly heavier than the font.
    smask = Image.new("L", (SW, SH), 0)
    ImageDraw.Draw(smask).text((tx, pad + SCRIPT_BASELINE * cs), line, font=sf, fill=255, anchor="ls", stroke_width=max(1, round(0.009 * cs)), stroke_fill=255)
    base = pad + SCRIPT_BASELINE * cs
    smask = smask.transform(smask.size, Image.AFFINE, (1, -0.1, 0.1 * base, 0, 1, 0), Image.BICUBIC)
    sl = Image.new("RGBA", (SW, SH), ink + (0,))
    sl.putalpha(smask)
    layer.alpha_composite(sl)
    # Wordmark: "Ph", aperture "o", "to" in ink; "Plus" in vertical gold.
    bl = pad + WORDMARK_BASELINE * cs
    wmask = Image.new("L", (SW, SH), 0)
    d = ImageDraw.Draw(wmask)
    d.text((tx, bl), "Ph", font=wf, fill=255, anchor="ls")
    o_left = tx + wf.getlength("Ph")
    ob = wf.getbbox("o", anchor="ls")
    dia = ob[3] - ob[1]
    cx, cy = o_left + (ob[0] + ob[2]) / 2, bl + (ob[1] + ob[3]) / 2
    ap = Image.open(assets / "brand/mark-aperture.png").convert("RGBA").resize((round(dia), round(dia)), Image.LANCZOS)
    wmask.paste(255, (round(cx - dia / 2), round(cy - dia / 2)), ap.split()[3])
    d.text((tx + wf.getlength("Pho"), bl), "to", font=wf, fill=255, anchor="ls")
    wl = Image.new("RGBA", (SW, SH), ink + (0,))
    wl.putalpha(wmask)
    layer.alpha_composite(wl)
    pmask = Image.new("L", (SW, SH), 0)
    ImageDraw.Draw(pmask).text((tx + wf.getlength("Photo"), bl), "Plus", font=wf, fill=255, anchor="ls")
    top_c, bot_c = ((242, 169, 0), (217, 121, 11)) if dark else ((255, 203, 46), (245, 160, 21))
    plus = Image.fromarray(np.dstack([_vgradient(SW, SH, bl - wsize * 0.72, bl, top_c, bot_c), np.asarray(pmask)]), "RGBA")
    layer.alpha_composite(plus)
    # Gold swoosh under the wordmark.
    L = (tx + 0.43 * cs, bl + 0.83 * cs)
    R = (tx + word_w - 0.74 * cs, bl + 0.74 * cs)
    P = (tx + 0.4 * word_w + 0.4 * cs, bl + 0.31 * cs)
    ctrl = (2 * P[0] - (L[0] + R[0]) / 2, 2 * P[1] - (L[1] + R[1]) / 2)
    thick = 0.22 * cs
    gmask = Image.new("L", (SW, SH), 0)
    ImageDraw.Draw(gmask).polygon(_quad(L, ctrl, R) + _quad(R, (ctrl[0], ctrl[1] + 2 * thick), L), fill=255)
    stops = [(0, (247, 183, 51)), (0.45, (255, 201, 60)), (1, (243, 154, 30))]
    layer.alpha_composite(Image.fromarray(np.dstack([_hgradient(SW, SH, L[0], R[0], stops), np.asarray(gmask)]), "RGBA"))
    layer = layer.resize((sw, sh), Image.LANCZOS)
    # Soft shadow from the silhouette (BlurMaskFilter 0.16 C, offset 0.05 C).
    blur = layer.split()[3].filter(ImageFilter.GaussianBlur(0.16 * c * 0.577 + 0.5))
    shifted = Image.new("L", (sw, sh), 0)
    shifted.paste(blur, (0, round(0.05 * c)))
    stamp = Image.new("RGBA", (sw, sh), shadow[:3] + (0,))
    stamp.putalpha(shifted.point(lambda v: v * shadow[3] // 255))
    stamp.alpha_composite(layer)
    return stamp, x, y, FREE_OPACITY[0] + (FREE_OPACITY[1] - FREE_OPACITY[0]) * opacity, dark


def watermarked(src: Path, dst: Path) -> dict:
    photo = Image.open(src).convert("RGB")
    stamp, x, y, op, dark = signature_mark(photo)
    r = run(demo, src, dst, "--stamp", pam(stamp, out / "stamp.pam"), x, y, f"{op:.3f}")
    r["darkInk"] = dark
    return r


# ---- frames (mirrors FrameStyles.kt) ------------------------------------------
frames_src = (root / "android/app/src/main/java/com/photoplus/edit/FrameStyles.kt").read_text()
SHAPES = {"RECT": 0, "ELLIPSE": 1, "ARCH": 2, "STAR": 3, "HEART": 4, "HEXAGON": 5, "DIAMOND": 6, "OCTAGON": 7, "SCALLOP": 8, "SPARKLE": 9}
BACKDROPS = {"SOLID": 0, "GRADIENT": 1, "BLURRED_PHOTO": 2, "PAPER": 3}


def frame_styles():
    styles = []
    for m in re.finditer(r'FrameStyle\("(\w+)", ([\d.]+)f, floatArrayOf\(([^)]*)\), FrameStyle\.(\w+)(.*?)\),\n', frames_src, re.S):
        fid, aspect, window, shape, rest = m.groups()
        s = dict(id=fid, aspect=float(aspect), window=[float(v.strip().rstrip("f")) for v in window.split(",")], shape=SHAPES[shape])
        kv = dict(re.findall(r"(\w+) = ([^,\n]+)", rest))
        num = lambda k, d=0.0: float(kv[k].rstrip("f")) if k in kv else d
        col = lambda k: ("0x" + re.search(r"0x([0-9A-F]{8})", kv[k]).group(1)) if k in kv and "0x" in kv[k] else ("0xFFDDB15E" if k in kv and "GOLD" in kv[k] else "0xFFFFFFFF")
        s.update(param=num("shapeParam"), rot=num("rotation"), backdrop=BACKDROPS[kv.get("backdrop", "FrameStyle.SOLID").split(".")[-1]], colorA=col("colorA"),
                 colorB=col("colorB"), border=num("border"), borderColor=col("borderColor"), metallic=int(kv.get("metallic", "false") == "true"),
                 gap=num("outlineGap"), ow=num("outlineWidth"), oc=col("outlineColor"), shadow=num("shadow"), so=num("shadowOffset", 0.02),
                 sb=num("shadowBlur", 0.04), glow=num("glow"), gc=col("glowColor"), deco=int(num("decoration")))
        styles.append(s)
    return styles


def framed(src: Path, dst: Path, s: dict) -> dict:
    w, h = Image.open(src).size
    window_aspect = s["window"][2] * s["aspect"] / s["window"][3]
    ww = min(w, h * window_aspect)
    cw = ww / s["window"][2]
    ch = cw / s["aspect"]
    return run(demo, src, dst, "--frame", round(cw), round(ch), s["shape"], s["param"], s["rot"], *s["window"], s["backdrop"], s["colorA"], s["colorB"],
               s["border"], s["borderColor"], s["metallic"], s["gap"], s["ow"], s["oc"], s["shadow"], s["so"], s["sb"], s["glow"], s["gc"], 1, 0, 0, s["deco"])


# ---- props fitted to a face (mirrors placement.ts) ---------------------------------
catalog = (root / "src/services/edit/catalog.ts").read_text()


def prop_def(pid):
    m = re.search(r"id: '%s',[^}]*\}" % pid, catalog).group(0)
    g = lambda k: re.search(r"\b%s: ([^,\n]+)" % k, m).group(1).strip(" '}")
    return dict(anchor=g("anchor"), aspect=eval(g("aspect")), ax=eval(g("ax")), ay=eval(g("ay")), fit=eval(g("fit")), drop=eval(g("drop")))


def face_of(photo: Image.Image):
    import mediapipe as mp
    from mediapipe.tasks.python import BaseOptions, vision

    # The landmarker is closed explicitly: letting it be collected mid-run
    # trips MediaPipe's shutdown handler.
    with vision.FaceLandmarker.create_from_options(
        vision.FaceLandmarkerOptions(base_options=BaseOptions(model_asset_path=str(landmarker)), num_faces=1)
    ) as lm:
        L = lm.detect(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.asarray(photo))).face_landmarks[0]
    P = lambda i: (L[i].x, L[i].y)
    eyes = sorted([P(468), P(473)])
    return dict(top=P(10), chin=P(152), eyeL=eyes[0], eyeR=eyes[1], width=abs(L[454].x - L[234].x))


def fit(d, face, W, H):
    a = W / H
    px = lambda p: (p[0] * a, p[1])
    eL, eR, top, chin = px(face["eyeL"]), px(face["eyeR"]), px(face["top"]), px(face["chin"])
    rx, ry = eR[0] - eL[0], eR[1] - eL[1]
    ed = math.hypot(rx, ry)
    rx, ry = rx / ed, ry / ed
    dx, dy = -ry, rx
    fh = (chin[0] - top[0]) * dx + (chin[1] - top[1]) * dy
    if d["anchor"] == "eyes":
        anc, w = ((eL[0] + eR[0]) / 2, (eL[1] + eR[1]) / 2), d["fit"] * ed
    else:
        anc, w = (top if d["anchor"] == "head" else chin), d["fit"] * face["width"] * a
    anc = (anc[0] + dx * d["drop"] * fh, anc[1] + dy * d["drop"] * fh)
    hh = w * d["aspect"]
    ox, oy = (0.5 - d["ax"]) * w, (0.5 - d["ay"]) * hh
    c = (anc[0] + rx * ox + dx * oy, anc[1] + ry * ox + dy * oy)
    return c[0] / a * W, c[1] * H, w / a * W, math.degrees(math.atan2(ry, rx))


def with_props(src: Path, dst: Path, ids) -> dict:
    photo = Image.open(src).convert("RGB")
    face = face_of(photo)
    args = []
    for pid in ids:
        cx, cy, w, rot = fit(prop_def(pid), face, *photo.size)
        args += ["--prop", pam(Image.open(assets / f"props/{pid}.png"), out / f"{pid}.pam"), f"{cx:.1f}", f"{cy:.1f}", f"{w:.1f}", f"{rot:.2f}", 0, 1, 1]
    return run(demo, src, dst, *args)


report = {}
k15, k18, k04, k23, k01 = ppm("kodim15"), ppm("kodim18"), ppm("kodim04"), ppm("kodim23"), ppm("kodim01")
# A high-key version of kodim08 (no Kodak frame is bright enough in the corner)
# exercises the dark ink.
k08 = out / "kodim08_highkey.ppm"
Image.open(kodak / "kodim08.png").convert("RGB").point(lambda v: round(255 * (v / 255) ** 0.35)).save(k08)
for name, src in [("wm_street", k01), ("wm_dark", k23), ("wm_portrait", k04), ("wm_bright", k08)]:
    report[name] = watermarked(src, out / f"{name}.ppm")
for s in frame_styles():
    report["frame_" + s["id"]] = framed(k04, out / f"frame_{s['id']}.ppm", s)
for pid in ["aviators", "round", "cateye", "tortoise", "hearts", "shield", "rimless"]:
    report[f"glasses_{pid}"] = with_props(k15, out / f"glasses_{pid}.ppm", [pid])
if portrait:
    src = out / "portrait.ppm"
    Image.open(portrait).convert("RGB").save(src)
    for pid in ["fedora", "beanie", "cap", "sunhat", "beret", "crown", "laurel", "tophat", "bucket", "cowboy", "flowers",
                "bowtie", "chain", "necktie", "pearls", "scarf", "choker", "medal"]:
        report[f"hat_{pid}"] = with_props(src, out / f"hat_{pid}.ppm", [pid])
report["camera_hearts"] = with_props(k04, out / "camera_hearts.ppm", ["hearts"])
report["edited"] = run(demo, k18, out / "edited.ppm", "--op", "10,0.25,0", "--op", "11,10,0.7", "--op", "1,0.2,0,0.1")
for p in out.glob("*.ppm"):
    Image.open(p).save(p.with_suffix(".png"))
(out / "editor_report.json").write_text(json.dumps(report, indent=1))
print(json.dumps(report))
