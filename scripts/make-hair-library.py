"""Build the Photoshoot Studio's photographed hairstyles from openly licensed photos.

For each style in scripts/hair-library.json (a source photo, its licence and
author, its natural colour), this:
  1. fetches the photo (cached; Wikimedia's standard thumbnail sizes, politely);
  2. finds the person's parts (MediaPipe Selfie Multiclass: hair, face, body,
     clothes) and cuts them out with the engine (studio_demo cutout: clean hair
     edges and the hair's own colour, without the old background's);
  3. keeps the hair only: the cut-out's cover where the parts map says hair
     (grown a little into the fine strands at its edge), never the face, the
     body or the clothes;
  4. writes android/app/src/main/assets/hair/<id>.webp (the hair, cropped, long
     edge at most 1024 px, WebP with transparency: about 150 KB) and <id>.json (the face box of the person it came
     from, in the image's pixels: how the engine fits it to another head; its
     natural colour; its credit), and src/services/studio/hairPhotos.json (the
     credits the app shows).

Usage (development machine, network on):
  python3 scripts/make-hair-library.py STUDIO_DEMO SELFIE_MULTICLASS.tflite CACHE_DIR [style ...]

Needs: numpy, pillow, opencv-python, mediapipe.
"""

import io
import json
import os
import subprocess
import sys
import time
import urllib.request

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks.python import BaseOptions, vision
from PIL import Image, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'android/app/src/main/assets/hair')
CREDITS = os.path.join(ROOT, 'src/services/studio/hairPhotos.json')
UA = {'User-Agent': 'PhotoPlusHairLibrary/1.0 (open-licence asset build)'}
MAX_EDGE = 1024


def fetch(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return
    for wait in (0, 30, 60, 120, 240):
        time.sleep(wait)
        try:
            data = urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=90).read()
            Image.open(io.BytesIO(data)).verify()
            with open(path, 'wb') as f:
                f.write(data)
            return
        except Exception as e:  # rate limits: wait and try again
            print('  fetch retry:', e)
    raise SystemExit(f'could not fetch {url}')


def build(style, demo, model, cache):
    sid = style['id']
    src = os.path.join(cache, sid + os.path.splitext(style['source']['file'])[1].lower())
    fetch(style['source']['file'], src)
    im = ImageOps.exif_transpose(Image.open(src)).convert('RGB')
    k = 1600 / max(im.size)
    if k < 1:
        im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
    img = np.asarray(im)
    seg = vision.ImageSegmenter.create_from_options(
        vision.ImageSegmenterOptions(base_options=BaseOptions(model_asset_path=model), output_confidence_masks=True, output_category_mask=True)
    )
    r = seg.segment(mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(img)))
    person = np.clip(1 - np.squeeze(r.confidence_masks[0].numpy_view()), 0, 1)
    zones = np.squeeze(r.category_mask.numpy_view()).astype(np.uint8)
    pre = os.path.join(cache, sid)
    im.save(pre + '.ppm')
    Image.fromarray((person * 255).astype(np.uint8)).save(pre + '_mask.pgm')
    Image.fromarray(zones).save(pre + '_zones.pgm')
    out = subprocess.run([demo, 'cutout', pre + '.ppm', pre + '_mask.pgm', pre + '_zones.pgm', pre], capture_output=True, text=True, check=True)
    report = json.loads(out.stdout.strip().splitlines()[-1])
    if not report.get('face'):
        raise SystemExit(f'{sid}: no face found in the source photo')
    fg = np.asarray(Image.open(pre + '_fg.ppm').convert('RGB'))
    a = np.asarray(Image.open(pre + '_alpha.pgm')).astype(np.float32) / 255
    H, W = a.shape
    fl, ft, fr, fb = [v * s for v, s in zip(report['faceBox'], (W, H, W, H))]
    fw = fr - fl
    hair = (zones == 1).astype(np.float32)
    face = (zones == 3).astype(np.float32)
    body = ((zones == 2) | (zones == 4) | (zones == 5)).astype(np.float32)
    ks = max(3, int(fw * 0.04)) | 1
    hw = cv2.GaussianBlur(cv2.dilate(hair, np.ones((ks, ks))), (0, 0), fw * 0.012)
    fs = cv2.GaussianBlur(cv2.dilate(face, np.ones((3, 3))), (0, 0), fw * 0.006)
    bs = cv2.GaussianBlur(body, (0, 0), fw * 0.01)
    cover = np.clip(a * np.clip(hw, 0, 1) * (1 - fs) * (1 - 0.9 * bs), 0, 1)
    ys, xs = np.nonzero(cover > 0.02)
    x0, x1 = max(0, xs.min() - 4), min(W, xs.max() + 5)
    y0, y1 = max(0, ys.min() - 4), min(H, ys.max() + 5)
    rgba = np.dstack([fg[y0:y1, x0:x1], (cover[y0:y1, x0:x1] * 255).astype(np.uint8)])
    s = min(1.0, MAX_EDGE / max(rgba.shape[:2]))
    out_im = Image.fromarray(rgba, 'RGBA')
    if s < 1:
        out_im = out_im.resize((round(out_im.width * s), round(out_im.height * s)), Image.LANCZOS)
    os.makedirs(ASSETS, exist_ok=True)
    out_im.save(os.path.join(ASSETS, sid + '.webp'), 'WEBP', quality=90, alpha_quality=90, method=6)
    meta = {
        'face': [round(float(v), 1) for v in ((fl - x0) * s, (ft - y0) * s, (fr - x0) * s, (fb - y0) * s)],
        'natural': style['natural'],
        'credit': style['source'],
    }
    with open(os.path.join(ASSETS, sid + '.json'), 'w') as f:
        json.dump(meta, f, indent=1)
    print(sid, out_im.size, meta['face'])
    return meta


def main():
    demo, model, cache = sys.argv[1:4]
    only = set(sys.argv[4:])
    os.makedirs(cache, exist_ok=True)
    styles = json.load(open(os.path.join(ROOT, 'scripts/hair-library.json')))
    credits = json.load(open(CREDITS)) if os.path.exists(CREDITS) else {}
    for st in styles:
        if only and st['id'] not in only:
            continue
        build(st, demo, model, cache)
        src = st['source']
        credits[st['id']] = {'author': src['author'], 'license': src['license'], 'licenseUrl': src['licenseUrl'], 'page': src['page']}
    with open(CREDITS, 'w') as f:
        json.dump(dict(sorted(credits.items())), f, indent=1)
        f.write('\n')


if __name__ == '__main__':
    main()
