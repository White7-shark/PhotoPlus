import { t as tx } from '../../i18n';
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
  { id: 'solid', name: tx('setup_plain'), about: tx('setup_one_clean_colour_as_web'), premium: false },
  { id: 'sweep', name: tx('setup_paper_sweep'), about: tx('setup_studio_paper_curving_from_the'), premium: false },
  { id: 'surface', name: tx('setup_tabletop'), about: tx('setup_a_table_against_a_wall'), premium: true },
  { id: 'radial', name: tx('setup_spotlit'), about: tx('setup_a_pool_of_colour_behind'), premium: true },
  { id: 'scene', name: tx('setup_scene'), about: tx('setup_a_real_place_behind_the'), premium: true },
  { id: 'transparent', name: tx('setup_transparent'), about: tx('setup_no_background_a_png_to'), premium: true },
];

export const BACKDROP_COLOURS: readonly { name: string; hex: string }[] = [
  { name: tx('setup_white'), hex: '#FFFFFF' },
  { name: tx('setup_paper'), hex: '#F4F2EE' },
  { name: tx('setup_light_grey'), hex: '#E4E4E4' },
  { name: tx('setup_stone'), hex: '#CFCAC2' },
  { name: tx('setup_sand'), hex: '#E8D8BF' },
  { name: tx('setup_blush'), hex: '#F2D7D3' },
  { name: tx('setup_sage'), hex: '#C8D5C2' },
  { name: tx('setup_sky'), hex: '#CDDFEE' },
  { name: tx('setup_terracotta'), hex: '#C47558' },
  { name: tx('setup_navy'), hex: '#1F2C44' },
  { name: tx('setup_charcoal'), hex: '#2A2A2D' },
  { name: tx('setup_black'), hex: '#101012' },
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
    name: tx('setup_centred'),
    about: tx('setup_in_the_middle_with_room'),
    premium: false,
    layout: { fill: 0.75, x: 0.5, y: 0.5, ground: false, flip: false },
  },
  {
    id: 'marketplace',
    name: tx('setup_fill_the_frame'),
    about: tx('setup_large_and_centred_as_marketplaces'),
    premium: false,
    layout: { fill: 0.85, x: 0.5, y: 0.5, ground: false, flip: false },
  },
  {
    id: 'standing',
    name: tx('setup_standing'),
    about: tx('setup_on_the_floor_a_little'),
    premium: false,
    layout: { fill: 0.72, x: 0.5, y: 0.86, ground: true, flip: false },
  },
  {
    id: 'left-third',
    name: tx('setup_left_third'),
    about: tx('setup_room_on_the_right_for'),
    premium: true,
    layout: { fill: 0.62, x: 0.34, y: 0.84, ground: true, flip: false },
  },
  {
    id: 'right-third',
    name: tx('setup_right_third'),
    about: tx('setup_room_on_the_left_for'),
    premium: true,
    layout: { fill: 0.62, x: 0.66, y: 0.84, ground: true, flip: false },
  },
  {
    id: 'hero',
    name: tx('setup_hero_close_up'),
    about: tx('setup_close_and_bold_filling_the'),
    premium: true,
    layout: { fill: 0.96, x: 0.5, y: 0.5, ground: false, flip: false },
  },
  {
    id: 'banner',
    name: tx('setup_banner'),
    about: tx('setup_small_and_low_for_wide'),
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
    name: tx('setup_square_1_1'),
    about: tx('setup_web_shops_and_marketplaces'),
    aspectW: 1,
    aspectH: 1,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'post',
    name: tx('setup_post_4_5'),
    about: tx('setup_social_feeds'),
    aspectW: 4,
    aspectH: 5,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'story',
    name: tx('setup_story_9_16'),
    about: tx('setup_stories_and_reels_covers'),
    aspectW: 9,
    aspectH: 16,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'catalogue',
    name: tx('setup_catalogue_3_4'),
    about: tx('setup_print_catalogues_and_lookbooks'),
    aspectW: 3,
    aspectH: 4,
    format: 'jpeg',
    quality: 95,
    premium: false,
  },
  {
    id: 'landscape',
    name: tx('setup_landscape_3_2'),
    about: tx('setup_websites_and_listings'),
    aspectW: 3,
    aspectH: 2,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'banner',
    name: tx('setup_banner_16_9'),
    about: tx('setup_website_headers_and_slides'),
    aspectW: 16,
    aspectH: 9,
    format: 'jpeg',
    quality: 92,
    premium: false,
  },
  {
    id: 'png',
    name: tx('setup_png_1_1'),
    about: tx('setup_lossless_transparent_when_the_backdrop'),
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
    name: tx('setup_marketplace_white'),
    about: tx('setup_pure_white_the_product_filling'),
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
    name: tx('setup_soft_paper'),
    about: tx('setup_warm_studio_paper_and_a'),
    premium: false,
    setup: with_({ kind: 'sweep', wall: '#EEEDEA', floor: '#FAF9F7' }, { contact: 0.6, soft: 0.4 }, {}, 'standing'),
    profile: 'post',
  },
  {
    id: 'studio-grey',
    name: tx('setup_studio_grey'),
    about: tx('setup_a_neutral_grey_sweep_that'),
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
    name: tx('setup_tabletop'),
    about: tx('setup_a_stone_wall_and_pale'),
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
    name: tx('setup_glossy_black'),
    about: tx('setup_black_a_mirror_finish_under'),
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
    name: tx('setup_pastel_pop'),
    about: tx('setup_blush_paper_and_a_spotlight'),
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
    name: tx('setup_cut_out_png'),
    about: tx('setup_no_background_a_soft_shadow'),
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
    name: tx('setup_catalogue_essentials'),
    about: tx('setup_white_paper_and_grey_the'),
    kind: 'product',
    premium: false,
    items: ['marketplace-white', 'soft-paper', 'studio-grey'],
  },
  {
    id: 'lifestyle',
    name: tx('setup_lifestyle_tabletop'),
    about: tx('setup_tables_window_light_and_mirror'),
    kind: 'product',
    premium: true,
    items: ['tabletop', 'glossy-black'],
  },
  {
    id: 'boutique',
    name: tx('setup_boutique'),
    about: tx('setup_spotlit_colour_and_cut_outs'),
    kind: 'product',
    premium: true,
    items: ['pastel-pop', 'cut-out'],
  },
  {
    id: 'everyday-looks',
    name: tx('setup_everyday_looks'),
    about: tx('setup_bright_true_and_warm_looks'),
    kind: 'look',
    premium: false,
    items: ['bright-clean', 'true-colour', 'warm-glow'],
  },
  {
    id: 'editorial-looks',
    name: tx('setup_editorial_looks'),
    about: tx('setup_moody_film_and_lush_colour'),
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
