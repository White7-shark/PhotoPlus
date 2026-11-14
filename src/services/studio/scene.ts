/**
 * The photoshoot studio's scenes and the decisions it makes about them
 * (pure, unit-tested): the background library, where a person goes by
 * default, the frame shape, and the look each scene starts with.
 *
 * Backgrounds are clean plates: photographs of places with nobody in them,
 * so nothing has to be invented behind the person. Built-in plates are real
 * CC0 photographs (Poly Haven); packs can add their own in the same format;
 * people can add their own photos of empty places.
 */
import rawPlates from './plates.json';

export type SceneCategory = 'studio' | 'indoors' | 'outdoors' | 'golden' | 'night' | 'mine' | 'pack' | 'plain';

export interface PlateDef {
  id: string;
  name: string;
  category: SceneCategory;
  width: number;
  height: number;
  /** The horizon: the camera's eye level (0 top … 1 bottom). */
  horizon: number;
  /** Height of the camera that took it, metres (0: a view with no floor to stand on). Measured for built-ins. */
  cameraHeight?: number;
  /** Farthest the floor reaches for feet (0..1 from the top), e.g. the base of the back wall. */
  back?: number;
  /** Nearest place for feet (0..1 from the top). */
  floor: number;
  /** Main light: -1 from the left … 1 from the right. */
  lightX: number;
  /** 0..1: hard light (sun) casts shadows on the ground. */
  castShadow: number;
  /** 0..1: backlight that rims the person's edge. */
  rim: number;
  /** Background blur this scene starts with (0..1). */
  blur: number;
  /** Built-in: app asset; mine/pack: a file in app storage. */
  source: 'builtin' | 'mine' | 'pack';
  /** File path (mine/pack) — built-ins resolve from their id. */
  path?: string;
  packId?: string;
  credit?: { source: string; asset: string; plate: string; authors: string[]; license: string; md5: string };
}

export const CATEGORY_LABELS: Record<SceneCategory, string> = {
  plain: 'Plain',
  studio: 'Studio',
  indoors: 'Indoors',
  outdoors: 'Outdoors',
  golden: 'Golden hour',
  night: 'Night',
  mine: 'My backgrounds',
  pack: 'From packs',
};

export const PLATES: readonly PlateDef[] = (rawPlates as Omit<PlateDef, 'source'>[]).map(p => ({ ...p, source: 'builtin' as const }));

/** The asset path of a built-in plate (full size) and its thumbnail. */
export const plateAsset = (id: string) => `studio/plates/${id}.jpg`;
export const thumbAsset = (id: string) => `studio/thumbs/${id}.jpg`;

/** Plain backgrounds: a colour, optionally a soft studio fall-off. */
export interface PlainDef {
  id: string;
  name: string;
  /** #rrggbb */
  color: string;
  /** Darker toward the edges, like a lit paper backdrop. */
  vignette: boolean;
}

export const PLAINS: readonly PlainDef[] = [
  { id: 'plain-white', name: 'White', color: '#F4F4F2', vignette: false },
  { id: 'plain-paper', name: 'Paper grey', color: '#B9BBBE', vignette: true },
  { id: 'plain-charcoal', name: 'Charcoal', color: '#2B2D31', vignette: true },
  { id: 'plain-black', name: 'Black', color: '#0B0B0C', vignette: false },
  { id: 'plain-cream', name: 'Cream', color: '#EFE3CF', vignette: true },
  { id: 'plain-blush', name: 'Blush', color: '#E9C3C6', vignette: true },
  { id: 'plain-sky', name: 'Sky blue', color: '#9FC3E7', vignette: true },
  { id: 'plain-emerald', name: 'Emerald', color: '#1F5B47', vignette: true },
];

export type Background =
  | { kind: 'plate'; plate: PlateDef }
  | { kind: 'plain'; plain: PlainDef }
  | { kind: 'transparent' };

/** A photo the person added as their own background. */
export function myPlate(id: string, name: string, path: string, width: number, height: number): PlateDef {
  return {
    id,
    name,
    category: 'mine',
    width,
    height,
    horizon: 0.5,
    cameraHeight: DEFAULT_CAMERA_METRES,
    back: 0.55,
    floor: 0.97,
    lightX: 0,
    castShadow: 0,
    rim: 0,
    blur: 0.3,
    source: 'mine',
    path,
  };
}

export function isPlateDef(v: unknown): v is PlateDef {
  if (!v || typeof v !== 'object') return false;
  const p = v as Record<string, unknown>;
  const unit = (k: string) => {
    const v = p[k];
    return typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 1;
  };
  return (
    typeof p.id === 'string' &&
    p.id.length > 0 &&
    p.id.length <= 80 &&
    typeof p.name === 'string' &&
    p.name.length <= 60 &&
    typeof p.category === 'string' &&
    p.category in CATEGORY_LABELS &&
    typeof p.width === 'number' &&
    p.width > 0 &&
    typeof p.height === 'number' &&
    p.height > 0 &&
    unit('horizon') &&
    unit('floor') &&
    (p.back === undefined || unit('back')) &&
    (p.cameraHeight === undefined || (typeof p.cameraHeight === 'number' && p.cameraHeight >= 0 && p.cameraHeight <= 30)) &&
    unit('castShadow') &&
    unit('rim') &&
    unit('blur') &&
    typeof p.lightX === 'number' &&
    Math.abs(p.lightX) <= 1 &&
    (p.source === 'builtin' || p.source === 'mine' || p.source === 'pack') &&
    (p.source === 'builtin' || typeof p.path === 'string')
  );
}

export const MAX_MY_BACKGROUNDS = 30;

export function platesByCategory(mine: readonly PlateDef[] = [], packs: readonly PlateDef[] = []) {
  const all = [...mine, ...packs, ...PLATES];
  const order: SceneCategory[] = ['mine', 'pack', 'studio', 'indoors', 'outdoors', 'golden', 'night'];
  return order
    .map(category => ({ category, plates: all.filter(p => p.category === category) }))
    .filter(g => g.plates.length > 0);
}

// ------------------------------------------------------------- the frame

export type FrameShape = 'scene' | 'portrait' | 'story' | 'square';

export const SHAPE_LABELS: Record<FrameShape, string> = {
  scene: 'Scene',
  portrait: 'Portrait 4:5',
  story: 'Story 9:16',
  square: 'Square',
};

const SHAPE_ASPECT: Record<Exclude<FrameShape, 'scene'>, number> = { portrait: 4 / 5, story: 9 / 16, square: 1 };

/** The part of the background used (fractions of it), for a shape, kept around `centreX`. */
export interface Crop {
  x: number;
  y: number;
  w: number;
  h: number;
}

export function cropFor(bgWidth: number, bgHeight: number, shape: FrameShape, centreX = 0.5): Crop {
  if (shape === 'scene' || bgWidth <= 0 || bgHeight <= 0) return { x: 0, y: 0, w: 1, h: 1 };
  const want = SHAPE_ASPECT[shape];
  const have = bgWidth / bgHeight;
  if (have > want) {
    // Wider than wanted: a vertical slice, as centred on the person as fits.
    const w = want / have;
    const x = Math.min(Math.max(centreX - w / 2, 0), 1 - w);
    return { x, y: 0, w, h: 1 };
  }
  const h = have / want;
  // Taller than wanted: keep the bottom (where people stand) a little more than the top.
  return { x: 0, y: (1 - h) * 0.7, w: 1, h };
}

/** Output size for a crop of a background, the long edge capped. */
export function frameSize(bgWidth: number, bgHeight: number, crop: Crop, maxEdge: number): { width: number; height: number } {
  let w = bgWidth * crop.w;
  let h = bgHeight * crop.h;
  const k = Math.min(1, maxEdge / Math.max(w, h));
  w = Math.round(w * k);
  h = Math.round(h * k);
  return { width: Math.max(16, w), height: Math.max(16, h) };
}

// ------------------------------------------------------------- placement

/** Where the person goes (the bottom centre of their box), in the frame (0..1). */
export interface Placement {
  x: number;
  y: number;
  /** Box height as a share of the frame height. */
  height: number;
  flip: boolean;
  tilt: number;
  angle: number;
}

/** What the cut-out report says about the person. */
export interface CutoutInfo {
  box: [number, number, number, number];
  /** The photo cuts them off at the bottom (a portrait), so they have no feet in it. */
  cutBottom: boolean;
  /** The photo cuts them off at the top (the head runs out of the photo). */
  cutTop?: boolean;
  /** The photo cuts them off at a side (shoulders run out of a close portrait). */
  cutSides?: boolean;
  /** Width / height of the source photo. */
  aspect: number;
  /** Their face (hairline to chin), in photo fractions, when it was found. */
  face?: [number, number, number, number] | null;
}

/** A standing adult, in metres, when nothing better is known. */
export const PERSON_METRES = 1.7;
/** An adult face from the hairline to the chin, in metres. */
export const FACE_METRES = 0.19;
/** A hand-held camera (people's own backgrounds have no measurements). */
export const DEFAULT_CAMERA_METRES = 1.5;

/** How tall the person's box really is, and where their eyes are in it. */
export interface Body {
  metres: number;
  /** Eyes, as a share of the box height from its top. */
  eyes: number;
  /** Measured from their face (not assumed). */
  measured: boolean;
}

export function bodyOf(cut: CutoutInfo): Body {
  const [, t, , b] = cut.box;
  const boxH = Math.max(b - t, 1e-3);
  const standing = !cut.cutBottom && !cut.cutTop;
  const face = cut.face;
  if (face && !cut.cutTop) {
    const fh = face[3] - face[1];
    if (fh > 0.012 && fh < boxH * 0.9) {
      const measured = (FACE_METRES * boxH) / fh;
      const eyes = clamp((face[1] - t + 0.42 * fh) / boxH, 0, 1);
      // A whole person: hairlines, hats and fringes make the face a little
      // uncertain, so the measurement is weighed with an adult's height.
      const metres = standing ? clamp(0.6 * measured + 0.4 * PERSON_METRES, 1.0, 2.05) : clamp(measured, 0.25, 1.8);
      return { metres, eyes, measured: true };
    }
  }
  if (standing) return { metres: PERSON_METRES, eyes: 0.065, measured: false };
  if (cut.cutTop) return { metres: 1.0, eyes: 0, measured: false };
  return { metres: 0.8, eyes: 0.22, measured: false };
}

/** A scene's ground, in the frame (after any crop): what makes sizes true. */
export interface Ground {
  horizon: number;
  /** Height of the camera above the floor, metres (0: no floor to stand on). */
  camera: number;
  /** Feet can go from `back` (farthest) to `floor` (nearest). */
  back: number;
  floor: number;
}

/**
 * The size of someone standing on level ground: a point at height z above
 * the floor, at a distance where the floor is at y, shows at
 * y - z (y - horizon) / camera. So a person `metres` tall with feet at y is
 * metres (y - horizon) / camera of the frame tall, whatever the lens. The
 * scale (metres / camera) is the same in any crop of the scene.
 */
export function heightOnGround(footY: number, horizon: number, scale: number): number {
  return clamp(scale * (footY - horizon), 0.02, 3);
}

/** The frame's horizon after a crop (the plate's horizon is in plate fractions). */
export function horizonInFrame(plateHorizon: number, crop: Crop): number {
  return (plateHorizon - crop.y) / crop.h;
}

/** The plate's ground in a crop of it. */
export function groundIn(plate: PlateDef, crop: Crop): Ground {
  const camera = plate.cameraHeight ?? DEFAULT_CAMERA_METRES;
  const back = plate.back ?? Math.min(plate.floor, plate.horizon + 0.04);
  return {
    horizon: horizonInFrame(plate.horizon, crop),
    camera,
    back: (back - crop.y) / crop.h,
    floor: (plate.floor - crop.y) / crop.h,
  };
}

/** The size the scene gives the person where they stand (null: no true size, e.g. a portrait). */
export function trueHeight(p: Placement, cut: CutoutInfo, ground: Ground | null): number | null {
  if (!ground || ground.camera <= 0 || cut.cutBottom || cut.cutTop || p.y <= ground.horizon + 0.01) return null;
  return heightOnGround(p.y, ground.horizon, bodyOf(cut).metres / ground.camera);
}

/** What auto framing chose: the crop of the background, the person's place, and the background blur. */
export interface Framing {
  crop: Crop;
  placement: Placement;
  bgBlur: number;
}

/** The person's share of the frame height the framing aims for. */
export function targetShare(shape: FrameShape, frameAspect: number): number {
  if (shape === 'square') return 0.66;
  if (shape === 'scene') return frameAspect > 1.2 ? 0.6 : 0.7;
  return 0.72;
}

/** The closest the framing zooms into a background (a share of its height), to keep it sharp. */
export const MIN_ZOOM = 0.66;

const place = (x: number, y: number, height: number): Placement => ({ x, y, height, flip: false, tilt: 0, angle: 0 });

/** Width / height of the person's box as seen. */
function boxAspectOf(cut: CutoutInfo) {
  const [l, t, r, b] = cut.box;
  return ((r - l) * cut.aspect) / Math.max(b - t, 1e-3);
}

/**
 * The best framing of a person in a background, true to the scene: someone
 * standing is sized by where they stand (their real height against the
 * camera's), the background zoomed a little when that leaves them small;
 * a portrait has its eyes brought near the horizon (where a camera at eye
 * level puts them). Closer people get a softer background, as a lens gives.
 */
export function autoFrame(cut: CutoutInfo, background: Background, shape: FrameShape, bgWidth: number, bgHeight: number): Framing {
  const body = bodyOf(cut);
  const boxAspect = boxAspectOf(cut);
  if (background.kind !== 'plate') {
    const aspect = bgWidth / Math.max(1, bgHeight);
    const placement = basicPlacement(cut, 0.55, 0.94, aspect, targetShare(shape, aspect));
    return { crop: { x: 0, y: 0, w: 1, h: 1 }, placement, bgBlur: 0 };
  }
  const plate = background.plate;
  const base = cropFor(bgWidth, bgHeight, shape, 0.5);
  const aspect = (bgWidth * base.w) / Math.max(1, bgHeight * base.h);
  const g = groundIn(plate, base);
  const T = Math.min(targetShare(shape, aspect), (0.9 * aspect) / Math.max(boxAspect, 0.05));
  const blurFor = (height: number) => {
    // Magnification (frame heights per metre): a full-length person ~0.4, a portrait ~1.
    const m = height / Math.max(body.metres, 0.2);
    return clamp(plate.blur * clamp(0.4 + 1.4 * m, 0.6, 2.2), 0, 0.85);
  };
  // Every crop of the plate with the frame's shape, from the shape's own
  // (largest) down to MIN_ZOOM of it, anywhere up and down the plate.
  const crops: Crop[] = [];
  for (let ci = 0; ci <= 30; ++ci) {
    const h = base.h * (1 - ((1 - MIN_ZOOM) * ci) / 30);
    const w = base.w * (h / base.h);
    const x = clamp(base.x + base.w / 2 - w / 2, 0, 1 - w);
    for (let yi = 0; yi <= 24; ++yi) crops.push({ x, y: ((1 - h) * yi) / 24, w, h });
  }
  const zoomCost = (c: Crop) => 0.9 * (1 - c.h / base.h);

  const standing = !cut.cutBottom && !cut.cutTop;
  const camera = plate.cameraHeight ?? DEFAULT_CAMERA_METRES;
  if (standing && camera > 0) {
    const scale = body.metres / camera;
    const back = plate.back ?? plate.horizon + 0.04;
    let best: { cost: number; crop: Crop; y: number; h: number } | null = null;
    for (const c of crops) {
      const hF = (plate.horizon - c.y) / c.h;
      const backF = (back - c.y) / c.h;
      for (let yi = 0; yi <= 90; ++yi) {
        // Feet in the frame, or a little below it (someone near the camera,
        // on the open floor in front of everything: a three-quarter shot).
        const yF = 0.36 + yi / 100;
        if (yF < backF) continue;
        const h = scale * (yF - hF);
        if (h <= 0.05 || yF - h < 0.025) continue; // the head stays in
        const cost = 3 * Math.abs(h - T) + Math.abs(yF - 0.89) + 5 * Math.max(0, yF - 0.97) + zoomCost(c);
        if (!best || cost < best.cost) best = { cost, crop: c, y: yF, h };
      }
    }
    if (best) return { crop: best.crop, placement: place(0.5, best.y, best.h), bgBlur: blurFor(best.h) };
  }
  if (cut.cutBottom && !cut.cutTop) {
    // A portrait: sized to the frame; its eyes are put near the horizon (a camera at eye level).
    let height = 0.8;
    if (!cut.cutSides) height = Math.min(height, (0.92 * aspect) / Math.max(boxAspect, 0.05));
    const eyes = 1 - height * (1 - body.eyes);
    let best = { cost: Infinity, crop: base };
    for (const c of crops) {
      const cost = Math.abs((plate.horizon - c.y) / c.h - eyes) + 0.4 * zoomCost(c) + 0.1 * Math.abs(c.y - base.y);
      if (cost < best.cost) best = { cost, crop: c };
    }
    return { crop: best.crop, placement: place(0.5, 1, height), bgBlur: blurFor(height) };
  }
  // Cut at the top, or a view with no floor to stand on.
  const placement = basicPlacement(cut, g.horizon, Math.min(0.97, g.floor), aspect, T);
  return { crop: base, placement, bgBlur: blurFor(placement.height) };
}

/** Framing without scene geometry (plain colours, views with no floor, people cut at the top). */
function basicPlacement(cut: CutoutInfo, horizon: number, floor: number, aspect: number, share: number): Placement {
  const boxAspect = boxAspectOf(cut);
  if (cut.cutTop && cut.cutBottom) return place(0.5, 1, 1);
  if (cut.cutBottom) {
    let height = 0.8;
    if (!cut.cutSides) height = Math.min(height, (0.92 * aspect) / Math.max(boxAspect, 0.05));
    return place(0.5, 1, height);
  }
  if (cut.cutTop) {
    const height = Math.min(1, (0.9 * aspect) / Math.max(boxAspect, 0.05));
    return place(0.5, height, height);
  }
  const height = Math.min(share, (0.9 * aspect) / Math.max(boxAspect, 0.05), floor - 0.03);
  return place(0.5, floor, height);
}

/**
 * A drag on the ground: the person moves, and (when following the scene's
 * perspective) their size follows the ground, keeping the proportion of
 * their true size they started with (someone made a little taller stays a
 * little taller).
 */
export function moveOnGround(p: Placement, x: number, y: number, horizon: number, followPerspective: boolean): Placement {
  const nx = Math.min(Math.max(x, -0.2), 1.2);
  const ny = Math.min(Math.max(y, 0.05), 1.3);
  if (!followPerspective || p.y >= 0.999 || p.y <= horizon + 0.02 || ny <= horizon + 0.02) {
    return { ...p, x: nx, y: ny };
  }
  // Height is proportional to the distance below the horizon (the scale cancels).
  const k = p.height / (p.y - horizon);
  return { ...p, x: nx, y: ny, height: Math.min(1.6, Math.max(0.03, k * (ny - horizon))) };
}

/** The person at `size` times their true size (or the auto size when the scene gives none). */
export function resized(p: Placement, size: number, reference: number): Placement {
  return { ...p, height: clamp(reference * clamp(size, SIZE_MIN, SIZE_MAX), 0.03, 1.6) };
}

export const SIZE_MIN = 0.5;
export const SIZE_MAX = 1.6;

/** Within this of 1, the size reads as true to the scene. */
export const TRUE_SIZE_TOLERANCE = 0.05;

function clamp(v: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, v));
}

// ---------------------------------------------------------------- the look

export interface SceneLook {
  colorMatch: number;
  lightWrap: number;
  shadow: number;
  castShadow: number;
  lightX: number;
  horizon: number;
  bgBlur: number;
  grain: number;
  rim: number;
  /** Spotlight 0..1: light on the person, the scene dimmed and softened around them. */
  spotlight: number;
}

export function lookFor(background: Background, crop: Crop): SceneLook {
  if (background.kind === 'plate') {
    const p = background.plate;
    return {
      colorMatch: 0.7,
      lightWrap: 0.35,
      shadow: 0.6,
      castShadow: p.castShadow,
      lightX: p.lightX,
      horizon: Math.min(1, Math.max(0, horizonInFrame(p.horizon, crop))),
      bgBlur: p.blur,
      grain: 1,
      rim: p.rim,
      spotlight: 0,
    };
  }
  // Plain colours: a studio sweep — a soft contact shadow, no colour cast to match.
  return { colorMatch: 0, lightWrap: 0.15, shadow: 0.45, castShadow: 0, lightX: 0, horizon: 0.55, bgBlur: 0, grain: 0.5, rim: 0, spotlight: 0 };
}
