import { t as tx } from '../../i18n';
/**
 * What the editor offers, under app names: looks, props, frames, fonts and
 * watermark styles. Free/Premium is decided here (and enforced natively for
 * anything that leaves the app). Ids are shared with the native renderer.
 */

export interface LookDef {
  id: string;
  /** pp_filter_id in the engine. */
  engineId: number;
  name: string;
  premium: boolean;
}

export const LOOKS: readonly LookDef[] = [
  { id: 'natural', engineId: 1, name: tx('catalog_natural'), premium: false },
  { id: 'vivid', engineId: 2, name: tx('catalog_vivid'), premium: false },
  { id: 'warm', engineId: 3, name: tx('catalog_warm'), premium: false },
  { id: 'cool', engineId: 4, name: tx('catalog_cool'), premium: false },
  { id: 'mono', engineId: 5, name: tx('catalog_mono'), premium: false },
  { id: 'fade', engineId: 6, name: tx('catalog_fade'), premium: false },
  { id: 'soft', engineId: 7, name: tx('catalog_soft'), premium: false },
  { id: 'noir', engineId: 8, name: tx('catalog_noir'), premium: false },
  { id: 'cinema', engineId: 9, name: tx('catalog_cinema'), premium: true },
  { id: 'golden', engineId: 10, name: tx('catalog_golden_hour'), premium: true },
  { id: 'film', engineId: 11, name: tx('catalog_film'), premium: true },
  { id: 'lush', engineId: 12, name: tx('catalog_lush'), premium: true },
];

export type PropCategory = 'hats' | 'glasses' | 'outfit' | 'mine';
/** Where a prop meets the body; 'free' props (made by people) go where they are put. */
export type PropAnchor = 'head' | 'eyes' | 'neck' | 'free';

export interface PropDef {
  id: string;
  name: string;
  category: PropCategory;
  premium: boolean;
  /** Height / width of the artwork. */
  aspect: number;
  anchor: PropAnchor;
  /** Where the prop meets the body, in artwork fractions (0..1). */
  ax: number;
  ay: number;
  /**
   * Size: the artwork's width is `fit` × face width (head and neck props) or
   * × the distance between the eyes (glasses).
   */
  fit: number;
  /** How far below the anchor line it sits, in face heights (negative: above). */
  drop: number;
  /** Artwork file for props people made (bundled props use propAsset). */
  uri?: string;
}

// Anchors are measured from the drawings in scripts/make-props.mjs.
export const PROPS: readonly PropDef[] = [
  // Hats: the crown meets the head near the top of the forehead, so the brim's
  // front edge lands just above the eyebrows (head width ≈ 1.3 face widths).
  {
    id: 'fedora',
    name: tx('catalog_fedora'),
    category: 'hats',
    premium: false,
    aspect: 0.56,
    anchor: 'head',
    ax: 0.5,
    ay: 0.705,
    fit: 1.3 / 0.476,
    drop: 0.05,
  },
  {
    id: 'beanie',
    name: tx('catalog_beanie'),
    category: 'hats',
    premium: false,
    aspect: 0.6,
    anchor: 'head',
    ax: 0.5,
    ay: 0.867,
    fit: 1.3 / 0.704,
    drop: 0.16,
  },
  {
    id: 'cap',
    name: tx('catalog_cap'),
    category: 'hats',
    premium: false,
    aspect: 0.56,
    anchor: 'head',
    ax: 0.5,
    ay: 0.76,
    fit: 1.28 / 0.6,
    drop: 0.1,
  },
  {
    id: 'sunhat',
    name: tx('catalog_sun_hat'),
    category: 'hats',
    premium: true,
    aspect: 0.48,
    anchor: 'head',
    ax: 0.5,
    ay: 0.704,
    fit: 1.3 / 0.45,
    drop: 0.06,
  },
  {
    id: 'beret',
    name: tx('catalog_beret'),
    category: 'hats',
    premium: true,
    aspect: 0.44,
    anchor: 'head',
    ax: 0.5,
    ay: 0.77,
    fit: 1.25 / 0.49,
    drop: 0.1,
  },
  {
    id: 'crown',
    name: tx('catalog_crown'),
    category: 'hats',
    premium: true,
    aspect: 0.47,
    anchor: 'head',
    ax: 0.5,
    ay: 0.872,
    fit: 1.15 / 0.62,
    drop: 0.02,
  },
  {
    id: 'laurel',
    name: tx('catalog_gold_laurel'),
    category: 'hats',
    premium: true,
    aspect: 0.42,
    anchor: 'head',
    ax: 0.5,
    ay: 0.73,
    fit: 1.25 / 0.66,
    drop: 0.0,
  },
  {
    id: 'tophat',
    name: tx('catalog_top_hat'),
    category: 'hats',
    premium: true,
    aspect: 0.64,
    anchor: 'head',
    ax: 0.5,
    ay: 0.734,
    fit: 1.3 / 0.4,
    drop: 0.05,
  },
  {
    id: 'bucket',
    name: tx('catalog_bucket_hat'),
    category: 'hats',
    premium: false,
    aspect: 0.52,
    anchor: 'head',
    ax: 0.5,
    ay: 0.635,
    fit: 1.3 / 0.48,
    drop: -0.04,
  },
  {
    id: 'cowboy',
    name: tx('catalog_cowboy_hat'),
    category: 'hats',
    premium: true,
    aspect: 0.56,
    anchor: 'head',
    ax: 0.5,
    ay: 0.679,
    fit: 1.3 / 0.4,
    drop: 0.06,
  },
  {
    id: 'flowers',
    name: tx('catalog_flower_crown'),
    category: 'hats',
    premium: false,
    aspect: 0.42,
    anchor: 'head',
    ax: 0.5,
    ay: 0.73,
    fit: 1.45 / 0.66,
    drop: 0.03,
  },
  // Glasses: the midpoint between the lens centres goes between the eyes.
  {
    id: 'aviators',
    name: tx('catalog_aviators'),
    category: 'glasses',
    premium: false,
    aspect: 0.38,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.526,
    fit: 1 / 0.4,
    drop: 0.01,
  },
  {
    id: 'round',
    name: tx('catalog_round'),
    category: 'glasses',
    premium: false,
    aspect: 0.36,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.528,
    fit: 1 / 0.4,
    drop: 0.0,
  },
  {
    id: 'cateye',
    name: tx('catalog_cat_eye'),
    category: 'glasses',
    premium: true,
    aspect: 0.38,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.526,
    fit: 1 / 0.47,
    drop: 0.0,
  },
  {
    id: 'tortoise',
    name: tx('catalog_tortoise'),
    category: 'glasses',
    premium: true,
    aspect: 0.36,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.54,
    fit: 1 / 0.4,
    drop: 0.0,
  },
  {
    id: 'hearts',
    name: tx('catalog_heart_shades'),
    category: 'glasses',
    premium: false,
    aspect: 0.38,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.5,
    fit: 1 / 0.4,
    drop: 0.0,
  },
  {
    id: 'shield',
    name: tx('catalog_sport_shield'),
    category: 'glasses',
    premium: true,
    aspect: 0.34,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.53,
    fit: 1 / 0.4,
    drop: 0.0,
  },
  {
    id: 'rimless',
    name: tx('catalog_gold_rimless'),
    category: 'glasses',
    premium: true,
    aspect: 0.36,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.514,
    fit: 1 / 0.4,
    drop: 0.0,
  },
  // Outfit pieces sit below the chin.
  {
    id: 'bowtie',
    name: tx('catalog_bow_tie'),
    category: 'outfit',
    premium: false,
    aspect: 0.32,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.5,
    fit: 1.05,
    drop: 0.42,
  },
  {
    id: 'chain',
    name: tx('catalog_gold_chain'),
    category: 'outfit',
    premium: false,
    aspect: 0.4,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.75,
    fit: 1.5,
    drop: 0.68,
  },
  {
    id: 'necktie',
    name: tx('catalog_tie'),
    category: 'outfit',
    premium: true,
    aspect: 0.7,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.136,
    fit: 1.4,
    drop: 0.4,
  },
  {
    id: 'pearls',
    name: tx('catalog_pearls'),
    category: 'outfit',
    premium: true,
    aspect: 0.42,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.786,
    fit: 1.6,
    drop: 0.75,
  },
  {
    id: 'scarf',
    name: tx('catalog_silk_scarf'),
    category: 'outfit',
    premium: true,
    aspect: 0.46,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.283,
    fit: 1.6,
    drop: 0.42,
  },
  {
    id: 'choker',
    name: tx('catalog_velvet_choker'),
    category: 'outfit',
    premium: false,
    aspect: 0.26,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.54,
    fit: 1.05,
    drop: 0.22,
  },
  {
    id: 'medal',
    name: tx('catalog_gold_medal'),
    category: 'outfit',
    premium: false,
    aspect: 0.78,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.05,
    fit: 1.1,
    drop: 0.35,
  },
];

export const PROP_CATEGORIES: readonly { id: PropCategory; label: string }[] = [
  { id: 'hats', label: tx('catalog_hats') },
  { id: 'glasses', label: tx('catalog_glasses') },
  { id: 'outfit', label: tx('catalog_outfit') },
  { id: 'mine', label: tx('catalog_mine') },
];

/** Where the artwork is bundled (Android assets). */
export const propAsset = (id: string) => `asset:/props/${id}.png`;

/** Where a prop's picture comes from (bundled, or a file for props people made). */
export const propSource = (def: PropDef) => def.uri ?? propAsset(def.id);

/** A prop someone made from their own photo (Premium), kept in settings. */
export interface CustomProp {
  id: string;
  name: string;
  anchor: PropAnchor;
  /** The cut-out (transparent PNG in app storage). */
  path: string;
  /** Height / width of the cut-out. */
  aspect: number;
  createdAt: number;
}

export const CUSTOM_PROP_ID = /^custom-[a-z0-9]{8,32}$/;
export const MAX_CUSTOM_PROPS = 60;

export const isCustomProps = (v: unknown): v is CustomProp[] =>
  Array.isArray(v) &&
  v.length <= MAX_CUSTOM_PROPS &&
  v.every(
    c =>
      c !== null &&
      typeof c === 'object' &&
      typeof c.id === 'string' &&
      CUSTOM_PROP_ID.test(c.id) &&
      typeof c.name === 'string' &&
      c.name.length <= 40 &&
      ['head', 'eyes', 'neck', 'free'].includes(c.anchor) &&
      typeof c.path === 'string' &&
      typeof c.aspect === 'number' &&
      c.aspect > 0 &&
      c.aspect < 20 &&
      typeof c.createdAt === 'number',
  );

/**
 * How a made prop sits, by where the person said it goes: on the head the
 * bottom of the cut-out meets the top of the forehead; glasses span the eyes;
 * neckwear hangs from below the chin; anywhere starts on the face.
 */
const CUSTOM_FIT: Record<PropAnchor, { ay: number; fit: number; drop: number }> = {
  head: { ay: 0.92, fit: 1.3, drop: 0.08 },
  eyes: { ay: 0.5, fit: 2.4, drop: 0 },
  neck: { ay: 0.1, fit: 1.2, drop: 0.35 },
  free: { ay: 0.5, fit: 1.2, drop: 0 },
};

export function customPropDef(c: CustomProp): PropDef {
  const f = CUSTOM_FIT[c.anchor];
  return {
    id: c.id,
    name: c.name,
    category: 'mine',
    premium: true,
    aspect: c.aspect,
    anchor: c.anchor,
    ax: 0.5,
    ay: f.ay,
    fit: f.fit,
    drop: f.drop,
    uri: `file://${c.path}`,
  };
}

/** Every prop available: the bundled ones, then those the person made. */
export const allProps = (custom: readonly CustomProp[] = []): PropDef[] => [...PROPS, ...custom.map(customPropDef)];

export const findProp = (id: string, custom: readonly CustomProp[] = []): PropDef | undefined =>
  PROPS.find(p => p.id === id) ?? custom.filter(c => c.id === id).map(customPropDef)[0];

export interface FrameDef {
  id: string;
  name: string;
  premium: boolean;
}

// The designs themselves (shapes, backdrops, borders) live in the native
// renderer (FrameStyles.kt) so preview and export are identical.
export const FRAMES: readonly FrameDef[] = [
  { id: 'gallery', name: tx('catalog_gallery'), premium: false },
  { id: 'polaroid', name: tx('catalog_instant'), premium: false },
  { id: 'arch', name: tx('catalog_arch'), premium: false },
  { id: 'circle', name: tx('catalog_halo'), premium: false },
  { id: 'soft', name: tx('catalog_soft_square'), premium: false },
  { id: 'story', name: tx('catalog_story'), premium: false },
  { id: 'tilted', name: tx('catalog_tilted_print'), premium: false },
  { id: 'noir', name: tx('catalog_noir'), premium: false },
  { id: 'doublemat', name: tx('catalog_double_mat'), premium: false },
  { id: 'capsule', name: tx('catalog_capsule'), premium: false },
  { id: 'star', name: tx('catalog_star'), premium: true },
  { id: 'heart', name: tx('catalog_heart'), premium: true },
  { id: 'hexagon', name: tx('catalog_hexagon'), premium: true },
  { id: 'film', name: tx('catalog_film'), premium: true },
  { id: 'diamond', name: tx('catalog_diamond'), premium: true },
  { id: 'window', name: tx('catalog_window'), premium: true },
  { id: 'scallop', name: tx('catalog_wavy'), premium: true },
  { id: 'octagon', name: tx('catalog_octagon'), premium: true },
  { id: 'sparkle', name: tx('catalog_sparkle'), premium: true },
  { id: 'neon', name: tx('catalog_neon'), premium: true },
  { id: 'cameo', name: tx('catalog_cameo'), premium: true },
  { id: 'sunset', name: tx('catalog_sunset_arch'), premium: true },
];

export interface FontDef {
  id: WatermarkFont;
  name: string;
  /** Font file in android/app/src/main/assets/fonts (React Native family name). */
  family: string;
}
export type WatermarkFont = 'modern' | 'elegant' | 'signature' | 'bold' | 'type';
export const FONTS: readonly FontDef[] = [
  { id: 'modern', name: tx('catalog_modern'), family: 'Poppins-SemiBold' },
  { id: 'elegant', name: tx('catalog_elegant'), family: 'DMSerifDisplay-Regular' },
  { id: 'signature', name: tx('catalog_signature'), family: 'GreatVibes-Regular' },
  { id: 'bold', name: tx('catalog_bold'), family: 'BebasNeue-Regular' },
  { id: 'type', name: tx('catalog_type'), family: 'SpaceMono-Regular' },
];

export type WatermarkStyle = 'classic' | 'pill' | 'signature' | 'minimal' | 'badge';
export const WATERMARK_STYLES: readonly { id: WatermarkStyle; name: string }[] = [
  { id: 'classic', name: tx('catalog_classic') },
  { id: 'pill', name: tx('catalog_pill') },
  { id: 'signature', name: tx('catalog_signature') },
  { id: 'minimal', name: tx('catalog_minimal') },
  { id: 'badge', name: tx('catalog_badge') },
];
