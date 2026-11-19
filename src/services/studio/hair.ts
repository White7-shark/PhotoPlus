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

/** What the person chose: a style, a colour, both, or neither (their own hair). */
export interface HairChoice {
  style?: string | null;
  color?: string | null;
}

export const hairStyle = (id: string | null | undefined) => HAIR_STYLES.find(s => s.id === id) ?? null;
export const stylesFor = (group: HairGroup) => HAIR_STYLES.filter(s => s.group === group);

/** How the device is told about the hair, or undefined for the person's own hair as it is. */
export function hairRequest(choice: HairChoice | null | undefined): { style?: string; color?: string } | undefined {
  if (!choice) return undefined;
  const style = choice.style && hairStyle(choice.style) ? choice.style : undefined;
  const color = choice.color && /^#[0-9a-f]{6}$/i.test(choice.color) ? choice.color.toUpperCase() : undefined;
  if (!style && !color) return undefined;
  return { ...(style ? { style } : {}), ...(color ? { color } : {}) };
}

/** A short line for what is chosen. */
export function hairSummary(choice: HairChoice | null | undefined): string {
  const style = hairStyle(choice?.style)?.name;
  const colour = HAIR_COLOURS.find(c => c.color.toUpperCase() === choice?.color?.toUpperCase())?.name ?? (choice?.color ? 'Your colour' : null);
  if (style && colour) return `${style} · ${colour}`;
  return style ?? colour ?? 'Your own hair';
}
