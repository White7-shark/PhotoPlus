/**
 * Hair in the Photoshoot Studio (Premium): a new colour for the person's
 * own hair, or a new hairstyle in its place: 23 for women, and dreadlocks,
 * rasta, an afro or bald for men. The styles are drawn on the device by the
 * engine strand by strand (native/engine/src/studio/hair.cpp: the ids here
 * are its ids), fitted to the person's head; their own hair is taken out first.
 */

export type HairGroup = 'women' | 'men';

export interface HairStyle {
  id: string;
  name: string;
  group: HairGroup;
}

const w = (id: string, name: string): HairStyle => ({ id, name, group: 'women' });
const m = (id: string, name: string): HairStyle => ({ id, name, group: 'men' });

export const HAIR_STYLES: readonly HairStyle[] = [
  w('long-straight', 'Long straight'),
  w('silk-press', 'Silk press'),
  w('long-waves', 'Long waves'),
  w('beach-waves', 'Beach waves'),
  w('long-curls', 'Long curls'),
  w('bob', 'Bob'),
  w('bob-bangs', 'Bob with bangs'),
  w('lob', 'Long bob'),
  w('pixie', 'Pixie'),
  w('curtain-bangs', 'Curtain bangs'),
  w('shag', 'Shag'),
  w('side-swept', 'Side swept'),
  w('top-knot', 'Top knot'),
  w('sleek-ponytail', 'Sleek ponytail'),
  w('curly-updo', 'Curly updo'),
  w('afro', 'Afro'),
  w('afro-puff', 'Afro puff'),
  w('twist-out', 'Twist-out'),
  w('bantu-knots', 'Bantu knots'),
  w('box-braids', 'Box braids'),
  w('cornrow-braids', 'Cornrow braids'),
  w('long-locs', 'Long locs'),
  w('finger-waves', 'Finger waves'),
  m('dreadlocks', 'Dreadlocks'),
  m('rasta', 'Rasta'),
  m('afro-men', 'Afro'),
  m('bald', 'Bald'),
];

export interface HairColour {
  id: string;
  name: string;
  /** #RRGGBB */
  color: string;
}

export const HAIR_COLOURS: readonly HairColour[] = [
  { id: 'jet-black', name: 'Jet black', color: '#121012' },
  { id: 'soft-black', name: 'Soft black', color: '#2A2220' },
  { id: 'dark-brown', name: 'Dark brown', color: '#3B2A20' },
  { id: 'chestnut', name: 'Chestnut', color: '#6A3E25' },
  { id: 'caramel', name: 'Caramel', color: '#A86B3C' },
  { id: 'honey-blonde', name: 'Honey blonde', color: '#C99A5B' },
  { id: 'golden-blonde', name: 'Golden blonde', color: '#D8B26E' },
  { id: 'platinum', name: 'Platinum', color: '#E6DFD2' },
  { id: 'ash-grey', name: 'Ash grey', color: '#A9A6A2' },
  { id: 'copper', name: 'Copper', color: '#B85C2E' },
  { id: 'auburn', name: 'Auburn', color: '#7D2E1E' },
  { id: 'burgundy', name: 'Burgundy', color: '#5E1A2A' },
  { id: 'rose-gold', name: 'Rose gold', color: '#C98B7E' },
  { id: 'pink', name: 'Pink', color: '#D46A9A' },
  { id: 'purple', name: 'Purple', color: '#5D3A8E' },
  { id: 'blue', name: 'Blue', color: '#2F5DA8' },
  { id: 'teal', name: 'Teal', color: '#1F7A78' },
  { id: 'ginger', name: 'Ginger', color: '#C4642C' },
];

/**
 * A photographer's own finishing of the style and colour (Premium). Shape
 * -1..1: length, volume, height, fringe, waves, shine; the parting -1 left
 * .. 1 right (null: the style's own). Fit: moved (face heights), sized,
 * turned (degrees). Colour: depth, darker roots (0..1), ends (-1 darker ..
 * 1 lighter). All at their defaults: as the app made it.
 */
export interface HairEdit {
  length: number;
  volume: number;
  height: number;
  fringe: number;
  part: number | null;
  waves: number;
  shine: number;
  moveX: number;
  moveY: number;
  size: number;
  rotate: number;
  depth: number;
  roots: number;
  ends: number;
}

export const NO_EDIT: HairEdit = {
  length: 0,
  volume: 0,
  height: 0,
  fringe: 0,
  part: null,
  waves: 0,
  shine: 0,
  moveX: 0,
  moveY: 0,
  size: 1,
  rotate: 0,
  depth: 0,
  roots: 0,
  ends: 0,
};

/** Hair brushes, painted on the photo: no new hair; the photo as it was; colour here too; no colour here. */
export type HairBrush = 'erase' | 'restore' | 'colourOn' | 'colourOff';
export const HAIR_BRUSH_ID: Record<HairBrush, number> = { erase: 0, restore: 1, colourOn: 2, colourOff: 3 };

export interface HairStroke {
  brush: HairBrush;
  /** Share of the photo's short side. */
  radius: number;
  /** Photo fractions: x0, y0, x1, y1, … */
  points: number[];
}

/** What the person chose: a style, a colour, both, or neither (their own hair), and their own finishing. */
export interface HairChoice {
  style?: string | null;
  color?: string | null;
  edit?: HairEdit;
  strokes?: readonly HairStroke[];
}

/** The edit's values that differ from the default (what the device is told). */
export function editRequest(e: HairEdit | undefined): Record<string, number> | undefined {
  if (!e) return undefined;
  const out: Record<string, number> = {};
  for (const k of Object.keys(NO_EDIT) as (keyof HairEdit)[]) {
    const v = e[k];
    if (k === 'part') {
      if (v != null && Number.isFinite(v)) out.part = Math.max(-1, Math.min(1, v));
      continue;
    }
    if (typeof v === 'number' && Number.isFinite(v) && v !== NO_EDIT[k]) out[k] = v;
  }
  return Object.keys(out).length > 0 ? out : undefined;
}

export const isEdited = (c: HairChoice | null | undefined) =>
  Boolean(editRequest(c?.edit)) || (c?.strokes?.length ?? 0) > 0;

export const hairStyle = (id: string | null | undefined) => HAIR_STYLES.find(s => s.id === id) ?? null;
export const stylesFor = (group: HairGroup) => HAIR_STYLES.filter(s => s.group === group);

/** How the device is told about the hair, or undefined for the person's own hair as it is. */
export function hairRequest(
  choice: HairChoice | null | undefined,
):
  | {
      style?: string;
      color?: string;
      edit?: Record<string, number>;
      strokes?: { brush: number; radius: number; points: number[] }[];
    }
  | undefined {
  if (!choice) return undefined;
  const style = choice.style && hairStyle(choice.style) ? choice.style : undefined;
  const color = choice.color && /^#[0-9a-f]{6}$/i.test(choice.color) ? choice.color.toUpperCase() : undefined;
  if (!style && !color) return undefined;
  const edit = editRequest(choice.edit);
  const strokes = (choice.strokes ?? []).map(s => ({
    brush: HAIR_BRUSH_ID[s.brush],
    radius: s.radius,
    points: [...s.points],
  }));
  return {
    ...(style ? { style } : {}),
    ...(color ? { color } : {}),
    ...(edit ? { edit } : {}),
    ...(strokes.length > 0 ? { strokes } : {}),
  };
}

/** A short line for what is chosen. */
export function hairSummary(choice: HairChoice | null | undefined): string {
  const style = hairStyle(choice?.style)?.name;
  const colour =
    HAIR_COLOURS.find(c => c.color.toUpperCase() === choice?.color?.toUpperCase())?.name ??
    (choice?.color ? 'Your colour' : null);
  if (style && colour) return `${style} · ${colour}`;
  return style ?? colour ?? 'Your own hair';
}
