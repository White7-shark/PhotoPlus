import { applyGesture, overlayRect, thin, toPhoto } from '../src/services/studio/gesture';
import {
  CATEGORY_LABELS,
  MIN_ZOOM,
  PERSON_METRES,
  PLAINS,
  PLATES,
  autoFrame,
  bodyOf,
  cropFor,
  frameSize,
  groundIn,
  heightOnGround,
  horizonInFrame,
  isPlateDef,
  lookFor,
  moveOnGround,
  myPlate,
  platesByCategory,
  resized,
  trueHeight,
  type CutoutInfo,
  type Placement,
} from '../src/services/studio/scene';
import { StudioError, StudioService, backgroundRequest, strokeRequest } from '../src/services/studio/StudioService';

describe('background library', () => {
  it('has clean plates of every kind, all valid and credited', () => {
    expect(PLATES.length).toBeGreaterThanOrEqual(15);
    const kinds = new Set(PLATES.map(p => p.category));
    for (const k of ['studio', 'indoors', 'outdoors', 'golden', 'night'] as const) expect(kinds.has(k)).toBe(true);
    for (const p of PLATES) {
      expect(isPlateDef(p)).toBe(true);
      expect(p.source).toBe('builtin');
      expect(p.credit?.license).toBe('CC0');
      expect(p.credit?.md5).toMatch(/^[0-9a-f]{32}$/);
      // Feet go below the horizon (people stand on the floor), from the back of the floor to its front.
      expect(p.floor).toBeGreaterThan(p.horizon);
      expect(p.back).toBeGreaterThan(p.horizon);
      expect(p.back).toBeLessThanOrEqual(p.floor);
      // Every built-in plate is measured: a camera height (0 only for a view with no floor).
      expect(p.cameraHeight).toBeGreaterThanOrEqual(0);
      expect(p.cameraHeight).toBeLessThan(3);
      expect(CATEGORY_LABELS[p.category]).toBeTruthy();
    }
    expect(new Set(PLATES.map(p => p.id)).size).toBe(PLATES.length);
    expect(PLAINS.every(p => /^#[0-9A-F]{6}$/i.test(p.color))).toBe(true);
  });

  it('adds my own backgrounds first, and refuses bad ones', () => {
    const mine = myPlate('bg-1', 'My background 1', '/data/backgrounds/a.jpg', 4000, 3000);
    expect(isPlateDef(mine)).toBe(true);
    const groups = platesByCategory([mine]);
    expect(groups[0].category).toBe('mine');
    expect(isPlateDef({ ...mine, path: undefined })).toBe(false);
    expect(isPlateDef({ ...mine, horizon: 2 })).toBe(false);
    expect(isPlateDef({ ...mine, category: 'moon' })).toBe(false);
    expect(isPlateDef(null)).toBe(false);
  });
});

describe('the frame', () => {
  it('crops a wide scene to a portrait slice around the person', () => {
    const c = cropFor(3000, 2000, 'portrait', 0.5);
    expect(c.h).toBe(1);
    expect((c.w * 3000) / 2000).toBeCloseTo(0.8, 5);
    expect(c.x).toBeCloseTo((1 - c.w) / 2, 5);
    // At the edge: the slice stops at the scene's edge.
    expect(cropFor(3000, 2000, 'story', 0.98).x + cropFor(3000, 2000, 'story', 0.98).w).toBeCloseTo(1, 5);
    expect(cropFor(3000, 2000, 'scene')).toEqual({ x: 0, y: 0, w: 1, h: 1 });
    // A tall scene made square keeps more of the bottom (the floor).
    const sq = cropFor(2000, 3000, 'square');
    expect(sq.w).toBe(1);
    expect(sq.y).toBeGreaterThan((1 - sq.h) / 2);
    const size = frameSize(3000, 2000, c, 1280);
    expect(Math.max(size.width, size.height)).toBe(1280);
    expect(horizonInFrame(0.5, { x: 0, y: 0.2, w: 1, h: 0.6 })).toBeCloseTo(0.5, 5);
  });
});

describe('placing a person', () => {
  const standing: CutoutInfo = { box: [0.3, 0.1, 0.7, 0.95], cutBottom: false, aspect: 0.75 };
  const portrait: CutoutInfo = { box: [0.1, 0.05, 0.9, 1], cutBottom: true, aspect: 0.75 };
  const plate = (id: string) => PLATES.find(p => p.id === id)!;
  const frame = (cut: CutoutInfo, id: string, shape: 'portrait' | 'scene' = 'portrait') => {
    const p = plate(id);
    return autoFrame(cut, { kind: 'plate', plate: p }, shape, p.width, p.height);
  };

  it('knows a person’s real size from their face, or assumes an adult', () => {
    expect(bodyOf(standing)).toMatchObject({ metres: PERSON_METRES, measured: false });
    // A face 1/9 of a whole person: an adult of about 1.7 m.
    const face = { ...standing, face: [0.45, 0.12, 0.55, 0.12 + 0.85 / 9] as [number, number, number, number] };
    expect(bodyOf(face).metres).toBeCloseTo(0.6 * 1.71 + 0.4 * 1.7, 1);
    expect(bodyOf(face).eyes).toBeGreaterThan(0.02);
    expect(bodyOf(face).eyes).toBeLessThan(0.1);
    // Head and shoulders: the face is a third of the box, so the box is about 0.6 m.
    const head = { ...portrait, face: [0.4, 0.2, 0.6, 0.2 + 0.95 / 3] as [number, number, number, number] };
    expect(bodyOf(head).metres).toBeCloseTo(0.57, 2);
    // A face too small to measure is not used.
    expect(bodyOf({ ...standing, face: [0.5, 0.1, 0.51, 0.105] }).measured).toBe(false);
  });

  it('sizes someone standing by where they stand: real height against the camera’s', () => {
    // Feet 0.3 of the frame below the horizon, a 1.7 m person, a 1 m camera: 0.51 of the frame.
    expect(heightOnGround(0.8, 0.5, 1.7 / 1.0)).toBeCloseTo(0.51, 5);
    // Further back (toward the horizon) is smaller; at the horizon they almost vanish.
    expect(heightOnGround(0.6, 0.5, 1.7)).toBeLessThan(heightOnGround(0.8, 0.5, 1.7));
    expect(heightOnGround(0.5, 0.5, 1.7)).toBeCloseTo(0.02, 5);
    // The rule holds in any crop of the scene (zooming changes both sides alike).
    const p = plate('grand-hall');
    const crop = { x: 0.2, y: 0.25, w: 0.6, h: 0.7 };
    const g = groundIn(p, crop);
    const footPlate = 0.9;
    const inPlate = heightOnGround(footPlate, p.horizon, PERSON_METRES / p.cameraHeight!);
    expect(heightOnGround((footPlate - crop.y) / crop.h, g.horizon, PERSON_METRES / g.camera)).toBeCloseTo(inPlate / crop.h, 5);
  });

  it('frames a standing person true to the scene, filling the frame well, feet on open floor', () => {
    for (const p of PLATES.filter(q => (q.cameraHeight ?? 0) > 0)) {
      const f = frame(standing, p.id);
      const g = groundIn(p, f.crop);
      // True to the scene where they stand.
      expect(trueHeight(f.placement, standing, g)).toBeCloseTo(f.placement.height, 5);
      // A good share of the frame, the head inside it, the feet on the plate's open floor
      // (or, near a low camera, just below the frame: a three-quarter shot).
      expect(f.placement.height).toBeGreaterThan(0.5);
      expect(f.placement.y - f.placement.height).toBeGreaterThan(0.02);
      const feet = f.crop.y + f.placement.y * f.crop.h;
      expect(feet).toBeGreaterThanOrEqual(p.back! - 1e-6);
      expect(f.placement.y).toBeLessThanOrEqual(1.1);
      // Never zoomed in past what keeps the background sharp, and inside the plate.
      expect(f.crop.h).toBeGreaterThanOrEqual(MIN_ZOOM * 0.999);
      expect(f.crop.x).toBeGreaterThanOrEqual(-1e-9);
      expect(f.crop.y + f.crop.h).toBeLessThanOrEqual(1 + 1e-9);
    }
  });

  it('fixes the park and the white room (people were far too small there)', () => {
    // The old rule gave about a quarter to a third of the frame; true size is much larger.
    expect(frame(standing, 'white-studio').placement.height).toBeGreaterThan(0.65);
    expect(frame(standing, 'autumn-park').placement.height).toBeGreaterThan(0.65);
    // A low camera with a near floor is not overfilled either: the person stands further back.
    const loft = frame(standing, 'sunlit-loft');
    expect(loft.placement.height).toBeLessThan(0.8);
  });

  it('puts a portrait’s eyes near the horizon and softens a close background more', () => {
    const f = frame(portrait, 'grand-hall');
    expect(f.placement.y).toBe(1);
    const eyes = 1 - f.placement.height * (1 - bodyOf(portrait).eyes);
    const without = Math.abs(horizonInFrame(plate('grand-hall').horizon, cropFor(plate('grand-hall').width, plate('grand-hall').height, 'portrait')) - eyes);
    expect(Math.abs(groundIn(plate('grand-hall'), f.crop).horizon - eyes)).toBeLessThanOrEqual(without + 1e-9);
    expect(f.bgBlur).toBeGreaterThan(frame(standing, 'grand-hall').bgBlur);
    // Not wider than the frame unless their photo already cuts them at the sides.
    const widthShare = (f.placement.height * (0.8 * 0.75)) / 0.95 / (0.8);
    expect(widthShare).toBeLessThanOrEqual(0.93);
    // Cut at the top and the bottom: the full height of the frame.
    expect(frame({ ...portrait, cutTop: true }, 'grand-hall').placement).toMatchObject({ y: 1, height: 1 });
  });

  it('frames people in front of a view with no floor, and on plain colours', () => {
    const vista = frame(standing, 'desert-sunset');
    expect(vista.placement.height).toBeGreaterThan(0.5);
    expect(trueHeight(vista.placement, standing, groundIn(plate('desert-sunset'), vista.crop))).toBeNull();
    const plain = autoFrame(standing, { kind: 'plain', plain: PLAINS[0] }, 'portrait', 800, 1000);
    expect(plain.crop).toEqual({ x: 0, y: 0, w: 1, h: 1 });
    expect(plain.placement.height).toBeGreaterThan(0.5);
    expect(plain.bgBlur).toBe(0);
  });

  it('scales by the Size slider against the true size, and drags keep the proportion', () => {
    const p: Placement = { x: 0.5, y: 0.9, height: 0.6, flip: false, tilt: 0, angle: 0 };
    expect(resized(p, 1.2, 0.5).height).toBeCloseTo(0.6, 5);
    expect(resized(p, 99, 0.5).height).toBeCloseTo(0.8, 5); // capped at 160%
    const start: Placement = { x: 0.5, y: 0.9, height: heightOnGround(0.9, 0.5, 1.7) * 1.1, flip: false, tilt: 0, angle: 0 };
    const back = moveOnGround(start, 0.4, 0.7, 0.5, true);
    expect(back.height).toBeCloseTo(heightOnGround(0.7, 0.5, 1.7) * 1.1, 5);
    expect(back.height).toBeLessThan(start.height);
    // Not following depth, or a portrait at the bottom edge: size stays.
    expect(moveOnGround(start, 0.4, 0.7, 0.5, false).height).toBe(start.height);
    expect(moveOnGround({ ...start, y: 1 }, 0.4, 0.95, 0.5, true).height).toBe(start.height);
  });

  it('moves with one finger and resizes with two', () => {
    const p: Placement = { x: 0.5, y: 0.9, height: 0.5, flip: false, tilt: 0, angle: 0 };
    const frame = { x: 0, y: 0, w: 400, h: 500 };
    const moved = applyGesture(p, [{ x: 200, y: 450 }], [{ x: 240, y: 450 }], frame, 0.4, false);
    expect(moved.x).toBeCloseTo(0.6, 5);
    expect(moved.height).toBe(0.5);
    const pinched = applyGesture(p, [{ x: 150, y: 300 }, { x: 250, y: 300 }], [{ x: 100, y: 300 }, { x: 300, y: 300 }], frame, 0.4, false);
    expect(pinched.height).toBeCloseTo(1.0, 5);
    expect(pinched.x).toBeCloseTo(0.5, 5);
    expect(applyGesture(p, [], [], frame, 0.4, false)).toBe(p);
  });

  it('draws the cut-out exactly where the engine puts the person', () => {
    const p: Placement = { x: 0.5, y: 0.9, height: 0.6, flip: false, tilt: 0, angle: 0 };
    const box = [0.25, 0.1, 0.75, 0.9] as const;
    const frame = { x: 10, y: 20, w: 300, h: 400 };
    const r = overlayRect(p, box, 0.75, frame);
    // The person's box: 60% of the frame tall, bottom centre on the anchor.
    const boxTop = r.top + box[1] * r.height;
    const boxBottom = r.top + box[3] * r.height;
    expect(boxBottom - boxTop).toBeCloseTo(0.6 * 400, 4);
    expect(boxBottom).toBeCloseTo(20 + 0.9 * 400, 4);
    expect(r.left + ((box[0] + box[2]) / 2) * r.width).toBeCloseTo(10 + 150, 4);
    // Mirrored: the box centre stays on the anchor.
    const m = overlayRect({ ...p, flip: true }, [0.1, 0.1, 0.5, 0.9], 0.75, frame);
    expect(m.left + (1 - 0.3) * m.width).toBeCloseTo(160, 4);
  });

  it('maps fingers to the photo and thins strokes', () => {
    const area = { x: 20, y: 0, w: 200, h: 300 };
    expect(toPhoto(120, 150, area)).toEqual({ x: 0.5, y: 0.5 });
    expect(toPhoto(0, 150, area)).toBeNull();
    const pts = Array.from({ length: 100 }, (_, i) => ({ x: 0.2 + i * 0.001, y: 0.5 }));
    const t = thin(pts, 0.03);
    expect(t.length / 2).toBeLessThan(15);
    expect(t.slice(-2)).toEqual([pts[99].x, pts[99].y]);
  });
});

describe('the look of a scene', () => {
  it('starts from the plate (its light, shadows and blur) and is plain for colours', () => {
    const sun = PLATES.find(p => p.id === 'sunrise-beach')!;
    const l = lookFor({ kind: 'plate', plate: sun }, { x: 0, y: 0, w: 1, h: 1 });
    expect(l.rim).toBe(sun.rim);
    expect(l.lightX).toBe(sun.lightX);
    expect(l.horizon).toBeCloseTo(sun.horizon, 5);
    const plain = lookFor({ kind: 'plain', plain: PLAINS[0] }, { x: 0, y: 0, w: 1, h: 1 });
    expect(plain.colorMatch).toBe(0);
    expect(plain.bgBlur).toBe(0);
  });
});

describe('studio service', () => {
  const native = {
    startSession: jest.fn(),
    refineCutout: jest.fn(),
    renderScene: jest.fn(),
    saveToGallery: jest.fn(),
    endSession: jest.fn(),
    cancel: jest.fn(),
  };
  const svc = new StudioService(native);
  const report = { coverage: 0.2, edge: 0.01, hairEdge: 0.3, box: [0.2, 0.1, 0.8, 1], cutLeft: false, cutTop: false, cutRight: false, cutBottom: true, found: true };

  it('asks for backgrounds and strokes the way the device expects', () => {
    expect(backgroundRequest({ kind: 'plate', plate: PLATES[0] })).toEqual({ kind: 'plate', id: PLATES[0].id });
    const mine = myPlate('bg-1', 'Mine', '/data/backgrounds/a.jpg', 10, 10);
    expect(backgroundRequest({ kind: 'plate', plate: mine })).toEqual({ kind: 'photo', path: '/data/backgrounds/a.jpg' });
    expect(backgroundRequest({ kind: 'plain', plain: PLAINS[1] })).toEqual({ kind: 'plain', color: PLAINS[1].color, vignette: true });
    expect(backgroundRequest({ kind: 'transparent' })).toEqual({ kind: 'transparent' });
    expect(strokeRequest([{ brush: 'hair', radius: 0.02, points: [0.1, 0.2] }])).toEqual([{ brush: 2, radius: 0.02, points: [0.1, 0.2] }]);
  });

  it('checks what comes back and names errors', async () => {
    native.startSession.mockResolvedValueOnce({ sessionId: 's1', photoPath: '/p.jpg', cutoutPath: '/c.png', width: 3, height: 4, report, parts: true });
    await expect(svc.start('/in.jpg', [{ x: 0.5, y: 0.5 }])).resolves.toMatchObject({ sessionId: 's1' });
    expect(native.startSession.mock.calls[0][1]).toEqual({ inputPath: '/in.jpg', points: [0.5, 0.5] });
    native.refineCutout.mockResolvedValueOnce({ cutoutPath: 7 });
    await expect(svc.refine('s1', [], { softness: 0, shift: 0, hairDetail: 0.5 })).rejects.toBeInstanceOf(StudioError);
    native.renderScene.mockRejectedValueOnce(Object.assign(new Error('Premium'), { code: 'PREMIUM_REQUIRED' }));
    await expect(
      svc.render('s1', { background: { kind: 'transparent' }, crop: { x: 0, y: 0, w: 1, h: 1 }, placement: { x: 0.5, y: 1, height: 1, flip: false, tilt: 0, angle: 0 }, look: lookFor({ kind: 'transparent' }, { x: 0, y: 0, w: 1, h: 1 }) }, 'full'),
    ).rejects.toMatchObject({ code: 'PREMIUM_REQUIRED' });
  });
});
