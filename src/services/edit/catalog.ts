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
  { id: 'natural', engineId: 1, name: 'Natural', premium: false },
  { id: 'vivid', engineId: 2, name: 'Vivid', premium: false },
  { id: 'warm', engineId: 3, name: 'Warm', premium: false },
  { id: 'cool', engineId: 4, name: 'Cool', premium: false },
  { id: 'mono', engineId: 5, name: 'Mono', premium: false },
  { id: 'fade', engineId: 6, name: 'Fade', premium: false },
  { id: 'soft', engineId: 7, name: 'Soft', premium: false },
  { id: 'noir', engineId: 8, name: 'Noir', premium: false },
  { id: 'cinema', engineId: 9, name: 'Cinema', premium: true },
  { id: 'golden', engineId: 10, name: 'Golden Hour', premium: true },
  { id: 'film', engineId: 11, name: 'Film', premium: true },
  { id: 'lush', engineId: 12, name: 'Lush', premium: true },
];

export type PropCategory = 'hats' | 'glasses' | 'outfit';
export type PropAnchor = 'head' | 'eyes' | 'neck';

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
}

// Anchors are measured from the drawings in scripts/make-props.mjs.
export const PROPS: readonly PropDef[] = [
  // Hats: the crown meets the head near the top of the forehead, so the brim's
  // front edge lands just above the eyebrows (head width ≈ 1.3 face widths).
  {
    id: 'fedora',
    name: 'Fedora',
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
    name: 'Beanie',
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
    name: 'Cap',
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
    name: 'Sun hat',
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
    name: 'Beret',
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
    name: 'Crown',
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
    name: 'Gold laurel',
    category: 'hats',
    premium: true,
    aspect: 0.42,
    anchor: 'head',
    ax: 0.5,
    ay: 0.73,
    fit: 1.25 / 0.66,
    drop: 0.0,
  },
  // Glasses: the midpoint between the lens centres goes between the eyes.
  {
    id: 'aviators',
    name: 'Aviators',
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
    name: 'Round',
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
    name: 'Cat-eye',
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
    name: 'Tortoise',
    category: 'glasses',
    premium: true,
    aspect: 0.36,
    anchor: 'eyes',
    ax: 0.5,
    ay: 0.54,
    fit: 1 / 0.4,
    drop: 0.0,
  },
  // Outfit pieces sit below the chin.
  {
    id: 'bowtie',
    name: 'Bow tie',
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
    name: 'Gold chain',
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
    name: 'Tie',
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
    name: 'Pearls',
    category: 'outfit',
    premium: true,
    aspect: 0.42,
    anchor: 'neck',
    ax: 0.5,
    ay: 0.786,
    fit: 1.6,
    drop: 0.75,
  },
];

export const PROP_CATEGORIES: readonly { id: PropCategory; label: string }[] = [
  { id: 'hats', label: 'Hats' },
  { id: 'glasses', label: 'Glasses' },
  { id: 'outfit', label: 'Outfit' },
];

/** Where the artwork is bundled (Android assets). */
export const propAsset = (id: string) => `asset:/props/${id}.png`;

export interface FrameDef {
  id: string;
  name: string;
  premium: boolean;
}

// The designs themselves (shapes, backdrops, borders) live in the native
// renderer (FrameStyles.kt) so preview and export are identical.
export const FRAMES: readonly FrameDef[] = [
  { id: 'gallery', name: 'Gallery', premium: false },
  { id: 'polaroid', name: 'Instant', premium: false },
  { id: 'arch', name: 'Arch', premium: false },
  { id: 'circle', name: 'Halo', premium: false },
  { id: 'soft', name: 'Soft square', premium: false },
  { id: 'star', name: 'Star', premium: true },
  { id: 'heart', name: 'Heart', premium: true },
  { id: 'hexagon', name: 'Hexagon', premium: true },
  { id: 'film', name: 'Film', premium: true },
  { id: 'diamond', name: 'Diamond', premium: true },
  { id: 'window', name: 'Window', premium: true },
];

export interface FontDef {
  id: WatermarkFont;
  name: string;
  /** Font file in android/app/src/main/assets/fonts (React Native family name). */
  family: string;
}
export type WatermarkFont = 'modern' | 'elegant' | 'signature' | 'bold' | 'type';
export const FONTS: readonly FontDef[] = [
  { id: 'modern', name: 'Modern', family: 'Poppins-SemiBold' },
  { id: 'elegant', name: 'Elegant', family: 'DMSerifDisplay-Regular' },
  { id: 'signature', name: 'Signature', family: 'GreatVibes-Regular' },
  { id: 'bold', name: 'Bold', family: 'BebasNeue-Regular' },
  { id: 'type', name: 'Type', family: 'SpaceMono-Regular' },
];

export type WatermarkStyle = 'classic' | 'pill' | 'signature' | 'minimal' | 'badge';
export const WATERMARK_STYLES: readonly { id: WatermarkStyle; name: string }[] = [
  { id: 'classic', name: 'Classic' },
  { id: 'pill', name: 'Pill' },
  { id: 'signature', name: 'Signature' },
  { id: 'minimal', name: 'Minimal' },
  { id: 'badge', name: 'Badge' },
];
