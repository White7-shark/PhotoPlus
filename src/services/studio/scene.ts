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
  /** Eye level of the camera that took it (0 top … 1 bottom). */
  horizon: number;
  /** Where feet go by default (0..1 from the top). */
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
    floor: 0.92,
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
}

/**
 * Standing people on level ground: a camera at about eye level sees the
 * horizon cross a standing person near their eyes (~90% of their height),
 * wherever they stand. So feet at y and a horizon at h give a height of
 * (y - h) / 0.9 — move someone back toward the horizon and they shrink as
 * they should.
 */
export const EYE_LEVEL = 0.9;

export function heightOnGround(footY: number, horizon: number): number {
  return Math.min(0.95, Math.max(0.12, (footY - horizon) / EYE_LEVEL));
}

/** The frame's horizon after a crop (the plate's horizon is in plate fractions). */
export function horizonInFrame(plateHorizon: number, crop: Crop): number {
  return (plateHorizon - crop.y) / crop.h;
}

export function suggestPlacement(cut: CutoutInfo, horizon: number, floor: number, frameAspect: number): Placement {
  const [l, t, r, b] = cut.box;
  const boxAspect = ((r - l) * cut.aspect) / Math.max(b - t, 1e-3); // width / height of the person's box
  if (cut.cutTop && cut.cutBottom) {
    // Cut off top and bottom: they fill the frame's height, as in their photo.
    return { x: 0.5, y: 1, height: 1, flip: false, tilt: 0, angle: 0 };
  }
  if (cut.cutBottom) {
    // A portrait: they enter from the bottom edge, head near the top third;
    // not wider than the frame unless their photo already cuts them at the sides.
    let height = 0.8;
    if (!cut.cutSides) height = Math.min(height, (0.92 * frameAspect) / Math.max(boxAspect, 0.05));
    return { x: 0.5, y: 1, height, flip: false, tilt: 0, angle: 0 };
  }
  if (cut.cutTop) {
    // Only the top cut: the head runs out of the top of the frame too.
    const height = Math.min(1, (0.9 * frameAspect) / Math.max(boxAspect, 0.05));
    return { x: 0.5, y: height, height, flip: false, tilt: 0, angle: 0 };
  }
  let height = heightOnGround(floor, horizon);
  height = Math.min(height, (0.9 * frameAspect) / Math.max(boxAspect, 0.05), floor - 0.03);
  return { x: 0.5, y: floor, height, flip: false, tilt: 0, angle: 0 };
}

/**
 * A drag on the ground: the person moves, and (when following the scene's
 * perspective) their size follows the horizon rule, keeping their
 * proportion of the rule they started with (someone placed a little taller
 * stays a little taller).
 */
export function moveOnGround(p: Placement, x: number, y: number, horizon: number, followPerspective: boolean): Placement {
  const nx = Math.min(Math.max(x, 0), 1);
  const ny = Math.min(Math.max(y, 0.05), 1.2);
  if (!followPerspective || p.y >= 0.999 || p.y <= horizon + 0.02 || ny <= horizon + 0.02) {
    return { ...p, x: nx, y: ny };
  }
  const k = p.height / heightOnGround(p.y, horizon);
  return { ...p, x: nx, y: ny, height: Math.min(1.5, Math.max(0.05, k * heightOnGround(ny, horizon))) };
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
    };
  }
  // Plain colours: a studio sweep — a soft contact shadow, no colour cast to match.
  return { colorMatch: 0, lightWrap: 0.15, shadow: 0.45, castShadow: 0, lightX: 0, horizon: 0.55, bgBlur: 0, grain: 0.5, rim: 0 };
}
