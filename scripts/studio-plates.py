#!/usr/bin/env python3
"""Builds the studio's background plates (developer tool, run by hand).

Clean plates: real photographs of places with nobody in them, so a person can
be placed in front without reconstructing anything that was never captured.
They are Poly Haven backplates (CC0, https://polyhaven.com/license): each is
downloaded, checked against Poly Haven's MD5, turned upright, reduced to a
2560 px long edge, and saved with a 360 px thumbnail into the app's assets.
The catalogue (src/services/studio/plates.json) carries what the studio needs
to place someone well: where the horizon is and how high the camera stood
(together they give the true size of a person anywhere on the floor), how far
back and how near the floor reaches, which side
the main light comes from, and whether it casts hard shadows.

  python3 scripts/studio-plates.py [--cache DIR]

Needs Pillow. Network: api.polyhaven.com, dl.polyhaven.org, cdn.polyhaven.com.
"""
import argparse, hashlib, json, os, urllib.request

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'android/app/src/main/assets/studio')
CATALOGUE = os.path.join(ROOT, 'src/services/studio/plates.json')
UA = [('User-Agent', 'asset-fetch/1.0')]

# id, Poly Haven asset, backplate, name, category, horizon, camera height (m),
# back (farthest feet: open floor, in front of furniture people would
# otherwise stand inside), floor (nearest feet), lightX, cast, rim, blur.
#
# Measured by hand on each plate from things of known size (a 45 cm side
# table, dining chairs, doors, railings, a studio cube) and which top faces
# are visible: an object of height Z standing at y, with its top at y_t, says
# Z / camera = (y - y_t) / (y - horizon). Level cameras put the horizon near
# the middle. A camera height of 0 means a view with no ground to stand on
# (a vista from a height): people are framed in front of it.
PLATES = [
    ('white-studio', 'photo_studio_broadway_hall', 'DSC_7392', 'White Studio', 'studio', 0.5, 1.15, 0.8, 0.95, -0.5, 0.0, 0.0, 0.15),
    ('rose-chair', 'photo_studio_broadway_hall', 'DSC_7402', 'Rose Chair', 'studio', 0.5, 0.9, 0.9, 0.96, 0.6, 0.0, 0.2, 0.3),
    ('boho-corner', 'brown_photostudio_02', '14', 'Boho Corner', 'studio', 0.47, 0.91, 0.92, 0.97, -0.4, 0.0, 0.0, 0.3),
    ('neon-lines', 'neon_photostudio', '03', 'Neon Lines', 'studio', 0.5, 1.65, 0.96, 0.99, 0.0, 0.0, 0.5, 0.2),
    ('sunlit-loft', 'photo_studio_loft_hall', 'DSC_8544', 'Sunlit Loft', 'studio', 0.36, 0.8, 0.56, 0.97, -0.6, 0.3, 0.3, 0.35),
    ('flower-hall', 'brown_photostudio_06', '07', 'Flower Hall', 'indoors', 0.52, 1.0, 0.88, 0.97, 0.6, 0.0, 0.2, 0.3),
    ('cafe-wall', 'comfy_cafe', 'DSC_6795', 'Café Wall', 'indoors', 0.43, 1.45, 0.9, 0.97, 0.0, 0.0, 0.0, 0.3),
    ('grand-hall', 'old_hall', 'DSC_4087', 'Grand Hall', 'indoors', 0.43, 1.6, 0.7, 0.97, 0.3, 0.0, 0.0, 0.25),
    ('autumn-park', 'autumn_park', 'DSC_0158', 'Autumn Park', 'outdoors', 0.58, 1.45, 0.62, 0.97, -0.3, 0.35, 0.0, 0.35),
    ('summer-meadow', 'meadow_2', '14', 'Summer Meadow', 'outdoors', 0.6, 1.5, 0.66, 0.97, 0.2, 0.3, 0.0, 0.3),
    ('garden-bench', 'studio_garden', '14', 'Garden Bench', 'outdoors', 0.28, 0.95, 0.6, 0.97, 0.0, 0.0, 0.0, 0.35),
    ('desert-sunset', 'kiara_8_sunset', '20160825-DSC01825', 'Desert Sunset', 'golden', 0.25, 0.0, 0.95, 0.97, -0.6, 0.3, 0.5, 0.3),
    ('sunrise-beach', 'umhlanga_sunrise', 'DSC08473', 'Sunrise Beach', 'golden', 0.375, 1.5, 0.72, 0.97, 0.7, 0.2, 0.6, 0.4),
    ('coastal-dusk', 'the_sky_is_on_fire', 'IMG_0844', 'Coastal Dusk', 'golden', 0.52, 1.2, 0.56, 0.97, -0.2, 0.0, 0.4, 0.3),
    ('night-piazza', 'vignaioli_night', 'DSC07544', 'Night Piazza', 'night', 0.5, 0.8, 0.56, 0.97, 0.0, 0.0, 0.3, 0.35),
]


def fetch_json(url):
    op = urllib.request.build_opener(); op.addheaders = UA
    with op.open(url, timeout=60) as r:
        return json.load(r)


def fetch(url, path):
    op = urllib.request.build_opener(); op.addheaders = UA
    with op.open(url, timeout=600) as r, open(path, 'wb') as f:
        f.write(r.read())


def upright(raw, thumb):
    """The files carry no orientation tag; Poly Haven's own previews are upright. Pick the turn that matches."""
    best = None
    small = raw.convert('L'); small.thumbnail((400, 400))
    t = thumb.convert('L')
    for ang in (0, 90, 270, 180):
        r = small.rotate(ang, expand=True)
        if (r.width > r.height) != (t.width > t.height):
            continue
        a = np.asarray(r.resize((64, 64))).astype(float); b = np.asarray(t.resize((64, 64))).astype(float)
        e = np.mean((a - a.mean() - b + b.mean()) ** 2)
        if best is None or e < best[0]:
            best = (e, ang)
    return best[1] if best else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cache', default=os.path.join(ROOT, '.cache', 'plates'))
    args = ap.parse_args()
    os.makedirs(args.cache, exist_ok=True)
    os.makedirs(os.path.join(ASSETS, 'plates'), exist_ok=True)
    os.makedirs(os.path.join(ASSETS, 'thumbs'), exist_ok=True)
    out = []
    for pid, asset, plate, name, cat, horizon, camera, back, floor, light, cast, rim, blur in PLATES:
        files = fetch_json(f'https://api.polyhaven.com/files/{asset}')['backplates'][plate]['jpg_plain']
        info = fetch_json(f'https://api.polyhaven.com/info/{asset}')
        raw_path = os.path.join(args.cache, f'{asset}__{plate}.jpg')
        if not os.path.exists(raw_path) or hashlib.md5(open(raw_path, 'rb').read()).hexdigest() != files['md5']:
            fetch(files['url'], raw_path)
        if hashlib.md5(open(raw_path, 'rb').read()).hexdigest() != files['md5']:
            raise SystemExit(f'{asset}/{plate}: checksum mismatch')
        thumb_path = os.path.join(args.cache, f'{asset}__{plate}_preview.jpg')
        if not os.path.exists(thumb_path):
            fetch(f'https://cdn.polyhaven.com/asset_img/backplates/{asset}/{plate}.jpg?width=300', thumb_path)
        raw = Image.open(raw_path).convert('RGB')
        im = raw.rotate(upright(raw, Image.open(thumb_path)), expand=True)
        k = 2560 / max(im.size)
        im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
        im.save(os.path.join(ASSETS, 'plates', f'{pid}.jpg'), quality=86, optimize=True, progressive=True)
        th = im.copy(); th.thumbnail((360, 360))
        th.save(os.path.join(ASSETS, 'thumbs', f'{pid}.jpg'), quality=80, optimize=True)
        out.append({
            'id': pid, 'name': name, 'category': cat,
            'width': im.width, 'height': im.height,
            'horizon': horizon, 'cameraHeight': camera, 'back': back, 'floor': floor, 'lightX': light, 'castShadow': cast, 'rim': rim, 'blur': blur,
            'credit': {'source': 'Poly Haven', 'asset': asset, 'plate': plate, 'authors': list(info.get('authors', {}).keys()),
                       'license': 'CC0', 'md5': files['md5']},
        })
        print(pid, im.size)
    with open(CATALOGUE, 'w') as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
        f.write('\n')


if __name__ == '__main__':
    main()
