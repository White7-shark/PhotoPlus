/**
 * Product photography (Phase 12): how a cut-out product becomes a catalogue
 * shot (mirrored and checked natively by Product.kt). A setup is the
 * backdrop, the shadows, the colour correction and where the product sits;
 * an export profile is the photo's shape and format (its size is a download
 * size). Catalogue presets and packs are ready-made setups.
 */

export type BackdropKind = 'transparent' | 'solid' | 'sweep' | 'surface' | 'radial' | 'scene';

export interface ProductSetup {
  backdrop: {
    kind: BackdropKind;
    /** #RRGGBB: the wall (solid: the only colour; radial: the centre). */
    wall: string;
    /** #RRGGBB: the floor (radial: the edges). */
    floor: string;
    /** 0.05..0.95 from the top: where the floor meets the wall. */
    horizon: number;
    /** 0..1: how gently the wall turns into the floor. */
    softness: number;
    /** 0..1: a pool of light behind the product, darker corners. */
    light: number;
    /** A studio scene photo (scene only). */
    plate?: string;
  };
  shadow: {
    contact: number;
    soft: number;
    cast: number;
    /** Degrees the cast shadow leans, -70..70. */
    castAngle: number;
    /** Of the product's height: > 0 behind it, < 0 in front. */
    castLength: number;
    castSoftness: number;
    reflection: number;
  };
  correction: {
    /** Take out the old backdrop's colour cast. */
    autoWhite: boolean;
    /** Bring a light backdrop up to white, and the product with it. */
    autoExposure: boolean;
    exposure: number;
    contrast: number;
    saturation: number;
    warmth: number;
    tint: number;
  };
  layout: {
    /** The product's larger side as a share of the photo's (0.1..1). */
    fill: number;
    x: number;
    y: number;
    /** y places the product's base (standing on a surface). */
    ground: boolean;
    flip: boolean;
  };
}

export const DEFAULT_SETUP: ProductSetup = {
  backdrop: { kind: 'sweep', wall: '#EEEDEA', floor: '#FAF9F7', horizon: 0.65, softness: 0.5, light: 0.5 },
  shadow: { contact: 0.6, soft: 0.35, cast: 0, castAngle: 25, castLength: 0.45, castSoftness: 0.5, reflection: 0 },
  correction: { autoWhite: true, autoExposure: false, exposure: 0, contrast: 0, saturation: 0, warmth: 0, tint: 0 },
  layout: { fill: 0.72, x: 0.5, y: 0.86, ground: true, flip: false },
};

export const BACKDROP_KINDS: readonly { id: BackdropKind; name: string; about: string; premium: boolean }[] = [
  { id: 'solid', name: 'Plain', about: 'One clean colour, as web shops ask for', premium: false },
  { id: 'sweep', name: 'Paper sweep', about: 'Studio paper curving from the wall into the floor', premium: false },
  { id: 'surface', name: 'Tabletop', about: 'A table against a wall', premium: true },
  { id: 'radial', name: 'Spotlit', about: 'A pool of colour behind the product', premium: true },
  { id: 'scene', name: 'Scene', about: 'A real place behind the product', premium: true },
  { id: 'transparent', name: 'Transparent', about: 'No background: a PNG to place anywhere', premium: true },
];

export const BACKDROP_COLOURS: readonly { name: string; hex: string }[] = [
  { name: 'White', hex: '#FFFFFF' },
  { name: 'Paper', hex: '#F4F2EE' },
  { name: 'Light grey', hex: '#E4E4E4' },
  { name: 'Stone', hex: '#CFCAC2' },
  { name: 'Sand', hex: '#E8D8BF' },
  { name: 'Blush', hex: '#F2D7D3' },
  { name: 'Sage', hex: '#C8D5C2' },
  { name: 'Sky', hex: '#CDDFEE' },
  { name: 'Terracotta', hex: '#C47558' },
  { name: 'Navy', hex: '#1F2C44' },
  { name: 'Charcoal', hex: '#2A2A2D' },
  { name: 'Black', hex: '#101012' },
];

/** Where the product sits: composition templates. */
export interface Template {
  id: string;
  name: string;
  about: string;
  premium: boolean;
  layout: ProductSetup['layout'];
}

export const TEMPLATES: readonly Template[] = [
  {
    id: 'centre',
    name: 'Centred',
    about: 'In the middle, with room around it',
    premium: false,
    layout: { fill: 0.75, x: 0.5, y: 0.5, ground: false, flip: false },
  },
  {
    id: 'marketplace',
    name: 'Fill the frame',
    about: 'Large and centred, as marketplaces ask (85%)',
    premium: false,
    layout: { fill: 0.85, x: 0.5, y: 0.5, ground: false, flip: false },
  },
  {
    id: 'standing',
    name: 'Standing',
    about: 'On the floor, a little below the middle',
    premium: false,
    layout: { fill: 0.72, x: 0.5, y: 0.86, ground: true, flip: false },
  },
  {
    id: 'left-third',
    name: 'Left third',
    about: 'Room on the right for text',
    premium: true,
    layout: { fill: 0.62, x: 0.34, y: 0.84, ground: true, flip: false },
  },
  {
    id: 'right-third',
    name: 'Right third',
    about: 'Room on the left for text',
    premium: true,
    layout: { fill: 0.62, x: 0.66, y: 0.84, ground: true, flip: false },
  },
  {
    id: 'hero',
    name: 'Hero close-up',
    about: 'Close and bold, filling the frame',
    premium: true,
    layout: { fill: 0.96, x: 0.5, y: 0.5, ground: false, flip: false },
  },
  {
    id: 'banner',
    name: 'Banner',
    about: 'Small and low, for wide headers',
    premium: true,
    layout: { fill: 0.5, x: 0.72, y: 0.88, ground: true, flip: false },
  },
];

/** How a photo is delivered: its shape and format (the size is a download size). */
export interface ExportProfile {
  id: string;
  name: string;
  about: string;
  aspectW: number;
  aspectH: number;
  format: 'jpeg' | 'png';
  quality: number;
  premium: boolean;
}

export const EXPORT_PROFILES: readonly ExportProfile[] = [
  {
    id: 'square',
    name: 'Square 1:1',
    about: 'Web shops and marketplaces',
    aspectW: 1,
    aspectH: 1,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'post',
    name: 'Post 4:5',
    about: 'Social feeds',
    aspectW: 4,
    aspectH: 5,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'story',
    name: 'Story 9:16',
    about: 'Stories and reels covers',
    aspectW: 9,
    aspectH: 16,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'catalogue',
    name: 'Catalogue 3:4',
    about: 'Print catalogues and lookbooks',
    aspectW: 3,
    aspectH: 4,
    format: 'jpeg',
    quality: 95,
    premium: false,
  },
  {
    id: 'landscape',
    name: 'Landscape 3:2',
    about: 'Websites and listings',
    aspectW: 3,
    aspectH: 2,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'banner',
    name: 'Banner 16:9',
    about: 'Website headers and slides',
    aspectW: 16,
    aspectH: 9,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'png',
    name: 'PNG 1:1',
    about: 'Lossless, transparent when the backdrop is',
    aspectW: 1,
    aspectH: 1,
    format: 'png',
    quality: 100,
    premium: true,
  },
];

export const exportProfile = (id: string) => EXPORT_PROFILES.find(p => p.id === id) ?? EXPORT_PROFILES[0];

/** Ready-made catalogue setups: one tap for a consistent look across a whole shop. */
export interface CatalogPreset {
  id: string;
  name: string;
  about: string;
  premium: boolean;
  setup: ProductSetup;
  profile: string;
}

const with_ = (
  b: Partial<ProductSetup['backdrop']>,
  s: Partial<ProductSetup['shadow']>,
  c: Partial<ProductSetup['correction']>,
  t: string,
) => ({
  backdrop: { ...DEFAULT_SETUP.backdrop, ...b },
  shadow: { ...DEFAULT_SETUP.shadow, contact: 0, soft: 0, ...s },
  correction: { ...DEFAULT_SETUP.correction, ...c },
  layout: { ...(TEMPLATES.find(x => x.id === t) ?? TEMPLATES[0]).layout },
});

export const CATALOG_PRESETS: readonly CatalogPreset[] = [
  {
    id: 'marketplace-white',
    name: 'Marketplace white',
    about: 'Pure white, the product filling 85% of a square, a soft touch of shadow',
    premium: false,
    setup: with_(
      { kind: 'solid', wall: '#FFFFFF', floor: '#FFFFFF', light: 0 },
      { contact: 0.35, soft: 0.2 },
      { autoExposure: true },
      'marketplace',
    ),
    profile: 'square',
  },
  {
    id: 'soft-paper',
    name: 'Soft paper',
    about: 'Warm studio paper and a natural shadow, the product standing',
    premium: false,
    setup: with_({ kind: 'sweep', wall: '#EEEDEA', floor: '#FAF9F7' }, { contact: 0.6, soft: 0.4 }, {}, 'standing'),
    profile: 'post',
  },
  {
    id: 'studio-grey',
    name: 'Studio grey',
    about: 'A neutral grey sweep that suits any product',
    premium: false,
    setup: with_(
      { kind: 'sweep', wall: '#CFCFCF', floor: '#E6E6E6', light: 0.6 },
      { contact: 0.65, soft: 0.45 },
      {},
      'standing',
    ),
    profile: 'square',
  },
  {
    id: 'tabletop',
    name: 'Tabletop',
    about: 'A stone wall and pale table, with window light throwing a shadow',
    premium: true,
    setup: with_(
      { kind: 'surface', wall: '#CFCAC2', floor: '#F1ECE4', horizon: 0.62, softness: 0.2, light: 0.6 },
      { contact: 0.7, soft: 0.3, cast: 0.55, castAngle: 35, castLength: 0.5 },
      {},
      'standing',
    ),
    profile: 'post',
  },
  {
    id: 'glossy-black',
    name: 'Glossy black',
    about: 'Black, a mirror finish under the product',
    premium: true,
    setup: with_(
      { kind: 'radial', wall: '#2A2A2D', floor: '#08080A', light: 0.4 },
      { contact: 0.5, reflection: 0.6 },
      { contrast: 0.15 },
      'standing',
    ),
    profile: 'square',
  },
  {
    id: 'pastel-pop',
    name: 'Pastel pop',
    about: 'Blush paper and a spotlight, made for beauty and gifts',
    premium: true,
    setup: with_(
      { kind: 'radial', wall: '#F7E3DF', floor: '#E9C3BC', light: 0.5 },
      { contact: 0.55, soft: 0.4 },
      { saturation: 0.1 },
      'centre',
    ),
    profile: 'post',
  },
  {
    id: 'cut-out',
    name: 'Cut-out PNG',
    about: 'No background, a soft shadow kept: drop it onto any design',
    premium: true,
    setup: with_({ kind: 'transparent' }, { contact: 0.5, soft: 0.3 }, {}, 'centre'),
    profile: 'png',
  },
];

/** Photo looks (editor colour) for any photo: products or people. */
export interface LookPreset {
  id: string;
  name: string;
  about: string;
  premium: boolean;
  /** EditDocument colour fields. */
  look: {
    exposure: number;
    brightness: number;
    contrast: number;
    saturation: number;
    warmth: number;
    sharpen: number;
    look: { id: string; strength: number } | null;
  };
}

const look = (
  id: string,
  name: string,
  about: string,
  premium: boolean,
  l: Partial<LookPreset['look']>,
): LookPreset => ({
  id,
  name,
  about,
  premium,
  look: { exposure: 0, brightness: 0, contrast: 0, saturation: 0, warmth: 0, sharpen: 0, look: null, ...l },
});

export const LOOK_PRESETS: readonly LookPreset[] = [
  look('bright-clean', 'Bright & clean', 'Lifted and crisp, true colour', false, {
    exposure: 0.25,
    brightness: 0.1,
    contrast: 0.05,
    sharpen: 0.3,
  }),
  look('true-colour', 'True colour', 'Neutral and sharp: what the eye saw', false, {
    contrast: 0.08,
    saturation: 0.05,
    sharpen: 0.35,
    look: { id: 'natural', strength: 0.6 },
  }),
  look('warm-glow', 'Warm glow', 'Golden and soft', false, {
    warmth: 0.25,
    brightness: 0.05,
    look: { id: 'warm', strength: 0.5 },
  }),
  look('moody', 'Moody', 'Deep shadows, quiet colour', true, {
    exposure: -0.15,
    contrast: 0.25,
    saturation: -0.15,
    look: { id: 'cinema', strength: 0.6 },
  }),
  look('film-day', 'Film day', 'Soft film colour with gentle grain', true, {
    brightness: 0.05,
    look: { id: 'film', strength: 0.75 },
  }),
  look('lush', 'Lush', 'Rich greens and blues', true, { saturation: 0.1, look: { id: 'lush', strength: 0.7 } }),
];

/**
 * Packs bring a set of setups or looks. Product packs suit product photos;
 * look packs suit any photo. A pack applies to projects it suits.
 */
export type ProjectKind = 'product' | 'portrait' | 'mixed';
export interface Pack {
  id: string;
  name: string;
  about: string;
  kind: 'product' | 'look';
  premium: boolean;
  items: string[];
}

export const PACKS: readonly Pack[] = [
  {
    id: 'catalogue-essentials',
    name: 'Catalogue essentials',
    about: 'White, paper and grey: the clean basics every shop needs',
    kind: 'product',
    premium: false,
    items: ['marketplace-white', 'soft-paper', 'studio-grey'],
  },
  {
    id: 'lifestyle',
    name: 'Lifestyle tabletop',
    about: 'Tables, window light and mirror finishes',
    kind: 'product',
    premium: true,
    items: ['tabletop', 'glossy-black'],
  },
  {
    id: 'boutique',
    name: 'Boutique',
    about: 'Spotlit colour and cut-outs for beauty, gifts and fashion',
    kind: 'product',
    premium: true,
    items: ['pastel-pop', 'cut-out'],
  },
  {
    id: 'everyday-looks',
    name: 'Everyday looks',
    about: 'Bright, true and warm: looks for any photo',
    kind: 'look',
    premium: false,
    items: ['bright-clean', 'true-colour', 'warm-glow'],
  },
  {
    id: 'editorial-looks',
    name: 'Editorial looks',
    about: 'Moody, film and lush colour for a magazine feel',
    kind: 'look',
    premium: true,
    items: ['moody', 'film-day', 'lush'],
  },
];

export const pack = (id: string | null | undefined) => PACKS.find(p => p.id === id) ?? null;

/** Whether a pack suits a project: product packs need products; looks suit any photo. */
export const packSuits = (p: Pack, kind: ProjectKind) => p.kind === 'look' || kind !== 'portrait';

// ---- Requests ----

const clamp = (v: number, lo: number, hi: number, d = 0) => (Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : d);
const HEX = /^#[0-9a-fA-F]{6}$/;
const hex = (v: string, d: string) => (HEX.test(v) ? v.toUpperCase() : d);

/** What the device is told (every value clamped as Product.kt does). */
export function setupRequest(s: ProductSetup) {
  const d = DEFAULT_SETUP;
  return {
    backdrop: {
      kind: s.backdrop.kind,
      wall: hex(s.backdrop.wall, d.backdrop.wall),
      floor: hex(s.backdrop.floor, d.backdrop.floor),
      horizon: clamp(s.backdrop.horizon, 0.05, 0.95, d.backdrop.horizon),
      softness: clamp(s.backdrop.softness, 0, 1),
      light: clamp(s.backdrop.light, 0, 1),
      ...(s.backdrop.kind === 'scene' && s.backdrop.plate ? { plate: s.backdrop.plate } : {}),
    },
    shadow: {
      contact: clamp(s.shadow.contact, 0, 1),
      soft: clamp(s.shadow.soft, 0, 1),
      cast: clamp(s.shadow.cast, 0, 1),
      castAngle: clamp(s.shadow.castAngle, -70, 70),
      castLength: clamp(s.shadow.castLength, -1.5, 1.5, d.shadow.castLength),
      castSoftness: clamp(s.shadow.castSoftness, 0, 1),
      reflection: clamp(s.shadow.reflection, 0, 1),
    },
    correction: {
      autoWhite: s.correction.autoWhite,
      autoExposure: s.correction.autoExposure,
      exposure: clamp(s.correction.exposure, -1, 1),
      contrast: clamp(s.correction.contrast, -1, 1),
      saturation: clamp(s.correction.saturation, -1, 1),
      warmth: clamp(s.correction.warmth, -1, 1),
      tint: clamp(s.correction.tint, -1, 1),
    },
    layout: {
      fill: clamp(s.layout.fill, 0.1, 1, d.layout.fill),
      x: clamp(s.layout.x, 0, 1, 0.5),
      y: clamp(s.layout.y, 0, 1, 0.5),
      ground: s.layout.ground,
      flip: s.layout.flip,
    },
  };
}

export function profileRequest(p: ExportProfile, resolution: number) {
  return { aspectW: p.aspectW, aspectH: p.aspectH, resolution, format: p.format, quality: p.quality };
}

/** Why saving a setup needs Premium (mirrors Product.kt), or null. */
export function setupPremium(s: ProductSetup): string | null {
  if (BACKDROP_KINDS.find(k => k.id === s.backdrop.kind)?.premium) return 'This backdrop is part of Premium';
  if (s.shadow.cast > 0 || s.shadow.reflection > 0) return 'Cast shadows and reflections are part of Premium';
  return null;
}

/** Saved setups are checked when read back (they come from storage). */
export function isProductSetup(v: unknown): v is ProductSetup {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, Record<string, unknown>>;
  const nums = (o: Record<string, unknown> | undefined, keys: string[]) =>
    !!o && keys.every(k => Number.isFinite(o[k]));
  return (
    !!s.backdrop &&
    BACKDROP_KINDS.some(k => k.id === s.backdrop.kind) &&
    typeof s.backdrop.wall === 'string' &&
    HEX.test(s.backdrop.wall) &&
    typeof s.backdrop.floor === 'string' &&
    HEX.test(s.backdrop.floor) &&
    nums(s.backdrop, ['horizon', 'softness', 'light']) &&
    nums(s.shadow, ['contact', 'soft', 'cast', 'castAngle', 'castLength', 'castSoftness', 'reflection']) &&
    nums(s.correction, ['exposure', 'contrast', 'saturation', 'warmth', 'tint']) &&
    typeof s.correction?.autoWhite === 'boolean' &&
    nums(s.layout, ['fill', 'x', 'y']) &&
    typeof s.layout?.ground === 'boolean'
  );
}

export function isLookValues(v: unknown): v is LookPreset['look'] {
  if (!v || typeof v !== 'object') return false;
  const l = v as Record<string, unknown>;
  const lk = l.look as { id?: unknown; strength?: unknown } | null;
  return (
    ['exposure', 'brightness', 'contrast', 'saturation', 'warmth', 'sharpen'].every(k => Number.isFinite(l[k])) &&
    (lk === null || (typeof lk?.id === 'string' && typeof lk.strength === 'number'))
  );
}
