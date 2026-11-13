"""
Builds the pose library (src/services/pose/poses.json) from short pose
descriptions, and a silhouette sheet to review them (development only).

Each pose is authored on a simple front-facing skeleton (units: one
shoulder width, origin at the shoulder centre, x toward the image's right,
y down): limb directions (degrees in the image: 0 = right, 90 = down,
-90 = up) or a hand target reached with two-bone arm geometry; head tilt and
turn; body turn and lean; then framed like a photo (head-and-shoulders, half
body, three-quarter or full body, at 3:4). The output is the 33 landmarks of
MediaPipe's pose landmarker, with the presence and visibility the model
would report (low for parts outside the frame or hidden), so a library pose
and a detected photo are the same kind of thing for the app.

  python3 scripts/pose-library.py OUT_JSON [SHEET.png]
"""
from __future__ import annotations

import json
import math
import sys

UA, FA, HAND = 0.82, 0.68, 0.22   # upper arm, forearm, wrist to knuckles
TH, SH, FOOT = 1.2, 1.15, 0.3     # thigh, shin, ankle to toes
TORSO, HIP = 1.3, 0.36            # shoulder line to hip line; half hip width
ASPECT = 0.75                     # 3:4 portrait frame


def d2v(deg: float, length: float) -> tuple[float, float]:
    r = math.radians(deg)
    return math.cos(r) * length, math.sin(r) * length


def ik(sx, sy, wx, wy, side):
    """Elbow for a shoulder at (sx, sy) reaching (wx, wy); side +1/-1 picks the bend."""
    dx, dy = wx - sx, wy - sy
    d = min(math.hypot(dx, dy), (UA + FA) * 0.999)
    a = math.atan2(dy, dx)
    cos_e = (UA * UA + d * d - FA * FA) / (2 * UA * d)
    e = math.acos(max(-1.0, min(1.0, cos_e)))
    ang = a + side * e
    return sx + math.cos(ang) * UA, sy + math.sin(ang) * UA


def build(p: dict) -> dict:
    yaw = math.radians(p.get('yaw', 0))
    cy = math.cos(yaw)
    pts = {}
    # Torso: shoulders (tilted), hips below (shifted by lean, tilted).
    st = math.radians(p.get('shoulderTilt', 0))
    half = 0.5 * cy
    pts[11] = (half * math.cos(st), half * math.sin(st), 0.5 * math.sin(yaw))
    pts[12] = (-half * math.cos(st), -half * math.sin(st), -0.5 * math.sin(yaw))
    lean = p.get('lean', 0) * TORSO / 60.0           # degrees → shoulder offset over hips
    ht = math.radians(p.get('hipTilt', 0))
    hx, hy = -lean, TORSO
    hh = HIP * cy
    pts[23] = (hx + hh * math.cos(ht), hy + hh * math.sin(ht), HIP * math.sin(yaw))
    pts[24] = (hx - hh * math.cos(ht), hy - hh * math.sin(ht), -HIP * math.sin(yaw))
    # Arms.
    for side, s, e, w, f in (('left', 11, 13, 15, 1), ('right', 12, 14, 16, -1)):
        arm = p[side + 'Arm']
        sx, sy, _ = pts[s]
        if arm[0] == 'ik':
            _, wx, wy, bend, *extra = arm
            ex, ey = ik(sx, sy, wx, wy, bend)
            reach = math.hypot(wx - ex, wy - ey)
            wx, wy = ex + (wx - ex) / reach * FA, ey + (wy - ey) / reach * FA
            # The hand continues the forearm unless a direction is given.
            hand = extra[0] if extra else math.degrees(math.atan2(wy - ey, wx - ex))
        else:
            # ('dir', upper, fore[, hand direction, upper length, fore length]):
            # lengths below 1 when a bone points toward the camera.
            _, u, fdeg, *more = arm
            hand_dir = more[0] if len(more) > 0 and more[0] is not None else fdeg
            ul = more[1] if len(more) > 1 else 1.0
            fl = more[2] if len(more) > 2 else 1.0
            ex, ey = sx + d2v(u, UA * ul)[0], sy + d2v(u, UA * ul)[1]
            wx, wy = ex + d2v(fdeg, FA * fl)[0], ey + d2v(fdeg, FA * fl)[1]
            hand = hand_dir
        pts[e] = (ex, ey, 0)
        pts[w] = (wx, wy, 0)
        hxv, hyv = d2v(hand, HAND)
        # pinky, index, thumb around the knuckles
        nx, ny = -hyv, hxv
        pts[17 if f > 0 else 18] = (wx + hxv * 0.9 + nx * 0.25 * f, wy + hyv * 0.9 + ny * 0.25 * f, 0)
        pts[19 if f > 0 else 20] = (wx + hxv, wy + hyv, 0)
        pts[21 if f > 0 else 22] = (wx + hxv * 0.6 - nx * 0.35 * f, wy + hyv * 0.6 - ny * 0.35 * f, 0)
    # Legs.
    for side, hp, kn, an, heel, toe, f in (('left', 23, 25, 27, 29, 31, 1), ('right', 24, 26, 28, 30, 32, -1)):
        thigh, shin, *rest = p[side + 'Leg']
        tl = rest[0] if rest else 1.0
        sx, sy, _ = pts[hp]
        kx, ky = sx + d2v(thigh, TH * tl)[0], sy + d2v(thigh, TH * tl)[1]
        ax, ay = kx + d2v(shin, SH)[0], ky + d2v(shin, SH)[1]
        pts[kn], pts[an] = (kx, ky, 0), (ax, ay, 0)
        pts[heel] = (ax - 0.05 * f, ay + 0.12, 0)
        pts[toe] = (ax + FOOT * 0.6 * f, ay + 0.16, 0)
    # Head: above the shoulder centre (neck follows the lean), tilted, turned.
    tilt = math.radians(p.get('headTilt', 0))
    turn = p.get('headTurn', 0.0)
    nxh = p.get('neck', 0.0) + (pts[11][0] + pts[12][0]) / 2
    nyh = (pts[11][1] + pts[12][1]) / 2 - 0.6  # measured on real portraits: 0.36–0.68
    face = {
        0: (0.0, 0.0), 1: (0.06, -0.1), 2: (0.1, -0.1), 3: (0.15, -0.1), 4: (-0.06, -0.1), 5: (-0.1, -0.1),
        6: (-0.15, -0.1), 7: (0.21, -0.05), 8: (-0.21, -0.05), 9: (0.06, 0.13), 10: (-0.06, 0.13),
    }
    for i, (fx, fy) in face.items():
        # Turning moves the face's front points toward the turn, the ears away.
        depth = 1.0 if i in (7, 8) else 0.0
        fx2 = fx * math.cos(turn * 1.2) + (turn * 0.21 if depth == 0 else -turn * 0.05)
        c, s = math.cos(tilt), math.sin(tilt)
        pts[i] = (nxh + fx2 * c - fy * s, nyh + fx2 * s + fy * c, 0)
    return pts


def frame(p: dict, pts: dict) -> tuple[list, dict]:
    head_top = min(y for _, y, _ in pts.values()) - 0.42
    shot = p['shot']
    if shot == 'headAndShoulders':
        bottom = 1.15
    elif shot == 'halfBody':
        bottom = TORSO + 0.55
    elif shot == 'threeQuarter':
        bottom = max(pts[25][1], pts[26][1]) + 0.35
    else:
        bottom = max(pts[31][1], pts[32][1], pts[29][1], pts[30][1]) + 0.3
    top = head_top - 0.25
    hb = bottom - top
    place = {'centre': 0.5, 'left': 0.4, 'right': 0.6}[p.get('place', 'centre')]
    # Fit sideways: everything that should be in the picture stays inside.
    keep = [i for i in pts if pts[i][1] <= bottom]
    xs = [pts[i][0] for i in keep]
    while True:
        w = hb * ASPECT
        if all(0.04 <= place + x / w <= 0.96 for x in xs):
            break
        hb *= 1.05
    w = hb * ASPECT
    hidden = set(p.get('hidden', []))
    out = []
    for i in range(33):
        x, y, z = pts[i]
        nx, ny, nz = place + x / w, (y - top) / hb, z / w
        inside = 0 <= nx <= 1 and 0 <= ny <= 1
        vis, pres = (0.98, 0.98) if inside else (0.3, 0.02)
        if i in hidden:
            vis = 0.12
        out.append([round(nx, 4), round(ny, 4), round(nz, 4), vis, pres])
    return out, {'scale': round(1 / hb, 4)}


DOWN_L, DOWN_R = ('dir', 80, 86), ('dir', 100, 94)  # arms relaxed at the sides
STAND_L, STAND_R = (88, 92), (92, 88)

POSES = [
    # ---- Free: simple demonstrations
    dict(id='classic-stand', name='Classic stand', category='standing', free=True, shot='fullBody',
         leftArm=DOWN_L, rightArm=DOWN_R, leftLeg=(86, 92), rightLeg=(96, 88), headTilt=-3, hipTilt=-4,
         tips=['Weight on your back foot, shoulders relaxed.', 'Chin slightly down and forward.']),
    dict(id='hand-on-hip', name='Hand on hip', category='standing', free=True, shot='threeQuarter',
         leftArm=('dir', 35, 145), rightArm=DOWN_R, leftLeg=STAND_L, rightLeg=(98, 86), hipTilt=-5, headTilt=4,
         tips=['Keep a gap between your arm and waist.', 'Turn your body a little toward the bent arm.']),
    dict(id='arms-crossed', name='Arms crossed', category='portrait', free=True, shot='halfBody',
         leftArm=('dir', 103, 172), rightArm=('dir', 77, 8), leftLeg=STAND_L, rightLeg=STAND_R,
         tips=['Relax your shoulders and smile softly.', 'Keep your hands loose, not gripping.']),
    dict(id='chin-rest', name='Chin rest', category='portrait', free=True, shot='headAndShoulders',
         leftArm=('dir', 55, -127, -100, 0.79, 1.1), rightArm=DOWN_R, leftLeg=STAND_L, rightLeg=STAND_R, headTilt=-4,
         tips=['Rest your chin lightly, don’t push your face up.', 'Show the side of your hand, not the palm.']),
    dict(id='over-the-shoulder', name='Over the shoulder', category='portrait', free=True, shot='halfBody',
         yaw=60, headTurn=-0.45, leftArm=DOWN_L, rightArm=DOWN_R, leftLeg=STAND_L, rightLeg=STAND_R, headTilt=-5,
         tips=['Turn your body away, then look back over your shoulder.', 'Drop the shoulder nearest the camera.']),
    dict(id='relaxed-lean', name='Relaxed lean', category='portrait', free=True, shot='halfBody',
         lean=8, headTilt=9, leftArm=DOWN_L, rightArm=('dir', 96, 100), leftLeg=STAND_L, rightLeg=STAND_R,
         tips=['Lean gently, as if against a wall.', 'Tilt your head with the lean.']),
    # ---- Premium
    dict(id='power-pose', name='Power pose', category='standing', shot='fullBody',
         leftArm=('dir', 35, 145), rightArm=('dir', 145, 35), leftLeg=(80, 95), rightLeg=(100, 85),
         tips=['Feet a little wider than your hips.', 'Chest open, chin level.']),
    dict(id='hands-in-pockets', name='Hands in pockets', category='standing', shot='threeQuarter',
         leftArm=('dir', 82, 118), rightArm=('dir', 98, 62), leftLeg=STAND_L, rightLeg=(97, 86), hipTilt=-4,
         hidden=[17, 18, 19, 20, 21, 22], tips=['Thumbs out or in, but keep the elbows soft.', 'Shift your weight to one leg.']),
    dict(id='walking-stride', name='Walking stride', category='standing', shot='fullBody',
         leftArm=('dir', 78, 66), rightArm=('dir', 104, 116), leftLeg=(82, 98), rightLeg=(104, 84),
         tips=['Walk slowly toward the camera and take the shot mid-step.', 'Let your arms swing naturally.']),
    dict(id='crossed-legs', name='Crossed legs', category='fashion', shot='fullBody',
         leftArm=DOWN_L, rightArm=('dir', 40, 140), leftLeg=(100, 84), rightLeg=(78, 97), hipTilt=-5, headTilt=-4,
         tips=['Cross one foot in front of the other.', 'Keep your weight on the back leg.']),
    dict(id='contrapposto', name='S-curve', category='fashion', shot='fullBody',
         shoulderTilt=4, hipTilt=-7, leftArm=('dir', 84, 96), rightArm=('dir', 102, 80), leftLeg=(84, 101),
         rightLeg=(91, 90), headTilt=5, tips=['Hip out to one side, shoulders the other way.', 'Bend the knee of the relaxed leg.']),
    dict(id='celebrate', name='Celebrate', category='fun', shot='fullBody',
         leftArm=('dir', -50, -72), rightArm=('dir', -130, -108), leftLeg=(84, 92), rightLeg=(96, 88),
         tips=['Arms up in a V, big smile.', 'Take a burst and pick the best one.']),
    dict(id='one-knee-up', name='Foot on the wall', category='standing', shot='fullBody',
         leftArm=('dir', 84, 92), rightArm=('dir', 100, 92), leftLeg=(40, 125, 0.75), rightLeg=(92, 90), lean=-4,
         tips=['Lean back on a wall and rest one foot against it.', 'Relax your arms or tuck one thumb in a pocket.']),
    dict(id='wave', name='Wave hello', category='fun', shot='halfBody',
         leftArm=('dir', -28, -82, -88), rightArm=DOWN_R, leftLeg=STAND_L, rightLeg=STAND_R, headTilt=4,
         tips=['Open hand, palm toward the camera.', 'Smile with your eyes.']),
    dict(id='hand-on-heart', name='Hand on heart', category='portrait', shot='halfBody',
         leftArm=DOWN_L, rightArm=('ik', 0.2, 0.45, -1), leftLeg=STAND_L, rightLeg=STAND_R, headTilt=-6,
         tips=['Soft hand, fingers together.', 'Look slightly down, then up at the lens.']),
    dict(id='hands-behind-head', name='Hands behind head', category='portrait', shot='halfBody',
         leftArm=('dir', -48, -159, None, 0.9, 1.0), rightArm=('dir', -132, -21, None, 0.9, 1.0), leftLeg=STAND_L, rightLeg=STAND_R,
         hidden=[15, 16, 17, 18, 19, 20, 21, 22], tips=['Elbows wide and open.', 'Lean back a little and relax your face.']),
    dict(id='collar-touch', name='Collar touch', category='fashion', shot='halfBody',
         leftArm=DOWN_L, rightArm=('ik', 0.28, 0.08, -1), leftLeg=STAND_L, rightLeg=STAND_R, yaw=20, headTilt=-4,
         tips=['Fingertips on the collar, light touch.', 'Turn your body slightly away from the hand.']),
    dict(id='side-profile', name='Side profile', category='portrait', shot='halfBody',
         yaw=75, headTurn=0.9, leftArm=DOWN_L, rightArm=DOWN_R, leftLeg=STAND_L, rightLeg=STAND_R,
         tips=['Turn fully to the side and look ahead.', 'Lift your chin a touch to define the jaw.']),
    dict(id='cheek-rest', name='Cheek rest', category='portrait', shot='headAndShoulders',
         leftArm=('dir', 77, -112, -100, 0.75, 1.15), rightArm=DOWN_R, leftLeg=STAND_L, rightLeg=STAND_R, headTilt=8,
         tips=['Rest your cheek on your hand and tilt into it.', 'Keep the hand soft and relaxed.']),
    dict(id='thinker', name='Thinker', category='portrait', shot='headAndShoulders',
         leftArm=('dir', 70, -112, -95, 0.75, 1.25), rightArm=('dir', 77, 8), leftLeg=STAND_L, rightLeg=STAND_R, headTilt=-3,
         tips=['One finger at the temple, the other arm across.', 'Look past the camera, thoughtful.']),
    dict(id='framing-hands', name='Framing hands', category='portrait', shot='headAndShoulders',
         leftArm=('dir', 84, -106, -95, 0.55, 1.05), rightArm=('dir', 96, -74, -85, 0.55, 1.05), leftLeg=STAND_L, rightLeg=STAND_R,
         tips=['Both hands softly beside your face.', 'Elbows in, close to your body.']),
    dict(id='hand-in-hair', name='Hand in hair', category='fashion', shot='halfBody',
         leftArm=DOWN_L, rightArm=('dir', -135, -34, -60, 0.76, 1.0), leftLeg=STAND_L, rightLeg=STAND_R, headTilt=-8,
         tips=['Run your hand back through your hair.', 'Tilt your head away from the raised arm.']),
    dict(id='peace-sign', name='Peace sign', category='fun', shot='headAndShoulders',
         leftArm=DOWN_L, rightArm=('dir', 144, -60, -95, 0.53, 1.0), leftLeg=STAND_L, rightLeg=STAND_R, headTilt=-6,
         tips=['Two fingers up beside your face.', 'Smile, chin a little down.']),
    dict(id='seated-relaxed', name='Seated, relaxed', category='sitting', shot='fullBody',
         leftArm=('ik', 0.62, 1.62, -1), rightArm=('ik', -0.62, 1.62, 1), leftLeg=(70, 92, 0.45), rightLeg=(110, 88, 0.45),
         tips=['Sit on the front of the chair, back straight.', 'Rest your hands on your thighs.']),
    dict(id='seated-knee', name='Seated, hands on knee', category='sitting', shot='fullBody',
         leftArm=('ik', 0.2, 1.75, -1), rightArm=('ik', 0.05, 1.75, 1), leftLeg=(60, 110, 0.4), rightLeg=(108, 86, 0.45),
         lean=-4, tips=['Cross one leg over the other.', 'Hold your knee with both hands.']),
]


def main() -> int:
    out_json = sys.argv[1]
    lib = []
    for p in POSES:
        pts = build(p)
        lms, info = frame(p, pts)
        lib.append({
            'id': p['id'], 'name': p['name'], 'category': p['category'], 'tier': 'free' if p.get('free') else 'premium',
            'shot': p['shot'], 'aspect': ASPECT, 'unit': info['scale'], 'tips': p['tips'], 'landmarks': lms,
        })
    with open(out_json, 'w') as f:
        json.dump({'version': 1, 'poses': lib}, f, separators=(',', ':'))
        f.write('\n')
    print(len(lib), 'poses,', sum(1 for p in lib if p['tier'] == 'free'), 'free')
    if len(sys.argv) > 2:
        sheet(lib, sys.argv[2])
    return 0


SEGMENTS = [(11, 13, 0.30), (13, 15, 0.24), (12, 14, 0.30), (14, 16, 0.24), (23, 25, 0.42), (25, 27, 0.33),
            (24, 26, 0.42), (26, 28, 0.33), (27, 31, 0.18), (28, 32, 0.18), (15, 19, 0.2), (16, 20, 0.2)]


def draw_silhouette(d, lms, w, h, sw, colour):
    """A filled body shape from the landmarks (as the app draws it)."""
    P = [(x * w, y * h) for x, y, *_ in lms]
    shoulder = math.dist(P[11], P[12])
    unit = max(sw * h, 1)
    def ok(i):
        return lms[i][4] > 0.5
    # torso
    if ok(11) and ok(12):
        hips = [P[23], P[24]] if ok(23) and ok(24) else [(P[11][0], P[11][1] + 1.3 * unit), (P[12][0], P[12][1] + 1.3 * unit)]
        d.polygon([P[12], P[11], (hips[0][0] + 0.08 * unit, hips[0][1]), (hips[1][0] - 0.08 * unit, hips[1][1])], fill=colour)
    for a, b, wd in SEGMENTS:
        if ok(a) and ok(b):
            d.line([P[a], P[b]], fill=colour, width=int(wd * unit))
            r = wd * unit / 2
            for q in (P[a], P[b]):
                d.ellipse([q[0] - r, q[1] - r, q[0] + r, q[1] + r], fill=colour)
    # neck and head
    if ok(0):
        cx, cy = P[0][0], P[0][1] - 0.05 * unit
        if ok(11) and ok(12):
            mid = ((P[11][0] + P[12][0]) / 2, (P[11][1] + P[12][1]) / 2)
            d.line([mid, (cx, cy)], fill=colour, width=int(0.3 * unit))
        d.ellipse([cx - 0.24 * unit, cy - 0.33 * unit, cx + 0.24 * unit, cy + 0.25 * unit], fill=colour)


def sheet(lib, path):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
    cw, ch = 180, 240
    cols = 7
    rows = (len(lib) + cols - 1) // cols
    img = Image.new('RGB', (cols * cw, rows * (ch + 24)), (24, 24, 30))
    d = ImageDraw.Draw(img)
    for k, p in enumerate(lib):
        x0, y0 = (k % cols) * cw, (k // cols) * (ch + 24)
        tile = Image.new('RGB', (cw - 6, ch), (40, 40, 52))
        td = ImageDraw.Draw(tile)
        draw_silhouette(td, p['landmarks'], tile.width, tile.height, p['unit'], (255, 255, 255))
        img.paste(tile, (x0 + 3, y0 + 22))
        tag = ('FREE ' if p['tier'] == 'free' else '') + p['name']
        d.text((x0 + 5, y0 + 3), tag, fill=(255, 210, 120) if p['tier'] == 'free' else (230, 230, 240), font=font)
    img.save(path)


if __name__ == '__main__':
    sys.exit(main())
