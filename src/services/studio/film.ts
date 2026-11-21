/**
 * Film & Collage (Photoshoot Studio): photos as frames on a film strip, or
 * in a collage, each frame with its own transformation (a look, a zoom, a
 * pan and a mirror). The device centres crops and zooms on each photo's face.
 */

/** The looks a frame can take, in the engine's order (pp_frame_look). */
export const FRAME_LOOKS = [
  { id: 'original', name: 'Original' },
  { id: 'bw', name: 'Black & white' },
  { id: 'sepia', name: 'Sepia' },
  { id: 'vintage', name: 'Vintage' },
  { id: 'cool', name: 'Cool' },
  { id: 'punch', name: 'Punchy' },
  { id: 'cross', name: 'Cross process' },
  { id: 'faded', name: 'Faded' },
  { id: 'golden', name: 'Golden' },
  { id: 'noir', name: 'Noir' },
] as const;
export type FrameLook = (typeof FRAME_LOOKS)[number]['id'];
export const lookIndex = (id: FrameLook) => FRAME_LOOKS.findIndex(l => l.id === id);

export interface FilmFrame {
  /** Index into the chosen photos. */
  photo: number;
  look: FrameLook;
  /** 1 (the whole photo) .. 4 */
  zoom: number;
  /** -1..1 each way, from the face. */
  panX: number;
  panY: number;
  flip: boolean;
}

export const plainFrame = (photo: number): FilmFrame => ({
  photo,
  look: 'original',
  zoom: 1,
  panX: 0,
  panY: 0,
  flip: false,
});

export const FILM_STYLES = [
  {
    id: 'classic',
    name: 'Classic 35mm',
    about: 'Black film, sprocket holes and edge numbers',
    premium: false,
    portrait: false,
  },
  { id: 'booth', name: 'Photo booth', about: 'A paper strip of portraits', premium: false, portrait: true },
  {
    id: 'amber',
    name: 'Amber negative',
    about: 'The warm orange base of colour negative film',
    premium: true,
    portrait: false,
  },
  { id: 'cinema', name: 'Cinema', about: 'Wide, cinematic frames', premium: true, portrait: false },
] as const;
export type FilmStyle = (typeof FILM_STYLES)[number]['id'];
export const filmStyle = (id: FilmStyle) => FILM_STYLES.find(s => s.id === id)!;

export interface FilmChoice {
  style: FilmStyle;
  vertical: boolean;
  /** Frames taller than wide. */
  portrait: boolean;
  markings: boolean;
  frames: FilmFrame[];
}

export const MAX_PHOTOS = 12;
export const MAX_FRAMES = 12;

/**
 * Variety for a strip or collage, as a photographer would build a contact
 * sheet: frames cycle through the photos, and each pass varies the look, the
 * crop (a closer frame on the face) and the direction, so even one photo
 * gives a whole strip of different frames.
 */
const MIX: Pick<FilmFrame, 'look' | 'zoom' | 'flip'>[] = [
  { look: 'original', zoom: 1, flip: false },
  { look: 'bw', zoom: 1.5, flip: false },
  { look: 'vintage', zoom: 1, flip: true },
  { look: 'noir', zoom: 2.2, flip: false },
  { look: 'golden', zoom: 1.3, flip: false },
  { look: 'cool', zoom: 1.8, flip: true },
  { look: 'faded', zoom: 1, flip: false },
  { look: 'cross', zoom: 1.5, flip: false },
];
export function mixedFrames(photos: number, count: number): FilmFrame[] {
  const n = Math.max(1, Math.min(MAX_FRAMES, Math.round(count)));
  const p = Math.max(1, photos);
  return Array.from({ length: n }, (_, i) => {
    // With several photos, each first appears as shot; variations come after.
    const pass = p > 1 ? Math.floor(i / p) : i;
    const m = MIX[pass % MIX.length];
    return { photo: i % p, look: m.look, zoom: m.zoom, panX: 0, panY: 0, flip: m.flip };
  });
}

/** The frames for `count` frames as shot (each photo in turn). */
export const plainFrames = (photos: number, count: number) =>
  Array.from({ length: Math.max(1, Math.min(MAX_FRAMES, count)) }, (_, i) => plainFrame(i % Math.max(1, photos)));

/** Keeps frames valid when photos are removed (frames of a removed photo move to the first). */
export function framesAfterRemoving<T extends { photo: number }>(frames: T[], removed: number): T[] {
  return frames.map(f =>
    f.photo === removed ? { ...f, photo: 0 } : f.photo > removed ? { ...f, photo: f.photo - 1 } : f,
  );
}

// ---- Collage ----

export interface CollageLayout {
  id: string;
  name: string;
  /** Cells as x, y, w, h (0..1). */
  cells: [number, number, number, number][];
}

const grid = (cols: number, rows: number): [number, number, number, number][] =>
  Array.from({ length: cols * rows }, (_, i) => [(i % cols) / cols, Math.floor(i / cols) / rows, 1 / cols, 1 / rows]);

export const COLLAGE_LAYOUTS: CollageLayout[] = [
  { id: 'duo', name: 'Side by side', cells: grid(2, 1) },
  { id: 'stack', name: 'Stacked', cells: grid(1, 2) },
  {
    id: 'hero3',
    name: 'One big, two small',
    cells: [
      [0, 0, 1, 0.6],
      [0, 0.6, 0.5, 0.4],
      [0.5, 0.6, 0.5, 0.4],
    ],
  },
  {
    id: 'side3',
    name: 'Tall and two',
    cells: [
      [0, 0, 0.6, 1],
      [0.6, 0, 0.4, 0.5],
      [0.6, 0.5, 0.4, 0.5],
    ],
  },
  { id: 'grid4', name: 'Grid of four', cells: grid(2, 2) },
  {
    id: 'hero4',
    name: 'One big, three small',
    cells: [
      [0, 0, 1, 0.62],
      [0, 0.62, 1 / 3, 0.38],
      [1 / 3, 0.62, 1 / 3, 0.38],
      [2 / 3, 0.62, 1 / 3, 0.38],
    ],
  },
  { id: 'strip4', name: 'Four in a row', cells: grid(1, 4) },
  {
    id: 'mosaic5',
    name: 'Mosaic',
    cells: [
      [0, 0, 0.6, 0.55],
      [0.6, 0, 0.4, 0.3],
      [0.6, 0.3, 0.4, 0.25],
      [0, 0.55, 0.4, 0.45],
      [0.4, 0.55, 0.6, 0.45],
    ],
  },
  { id: 'grid6', name: 'Grid of six', cells: grid(2, 3) },
  { id: 'grid9', name: 'Grid of nine', cells: grid(3, 3) },
];
export const collageLayout = (id: string) => COLLAGE_LAYOUTS.find(l => l.id === id) ?? COLLAGE_LAYOUTS[0];

export const COLLAGE_ASPECTS = [
  { id: 'portrait', name: '4:5', value: 0.8 },
  { id: 'square', name: '1:1', value: 1 },
  { id: 'story', name: '9:16', value: 9 / 16 },
  { id: 'landscape', name: '3:2', value: 1.5 },
] as const;
export type CollageAspect = (typeof COLLAGE_ASPECTS)[number]['id'];

export const COLLAGE_BACKGROUNDS = ['#FFFFFF', '#F6F1EA', '#111114', '#E9D7C3', '#F4D6DC', '#1E2A3A'] as const;

export interface CollageChoice {
  layout: string;
  aspect: CollageAspect;
  /** 0..0.1 of the short side */
  gap: number;
  /** 0..0.5 of a cell's short side */
  radius: number;
  background: string;
  frames: FilmFrame[];
}

// ---- Requests ----

const clamp = (v: number, lo: number, hi: number) => (Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : lo);
const frameRequest = (f: FilmFrame) => ({
  photo: f.photo,
  look: Math.max(0, lookIndex(f.look)),
  zoom: clamp(f.zoom, 1, 4),
  panX: clamp(f.panX, -1, 1),
  panY: clamp(f.panY, -1, 1),
  flip: f.flip,
});

export function filmRequest(photos: string[], film: FilmChoice, quality: 'preview' | 'full') {
  return {
    photos,
    frames: film.frames.slice(0, MAX_FRAMES).map(frameRequest),
    style: film.style,
    vertical: film.vertical,
    portrait: film.portrait,
    markings: film.markings,
    quality,
  };
}

export function collageRequest(photos: string[], c: CollageChoice, quality: 'preview' | 'full') {
  const layout = collageLayout(c.layout);
  return {
    photos,
    cells: layout.cells.map(([x, y, w, h], i) => ({
      x,
      y,
      w,
      h,
      ...frameRequest(c.frames[i] ?? plainFrame(i % Math.max(1, photos.length))),
    })),
    aspect: COLLAGE_ASPECTS.find(a => a.id === c.aspect)?.value ?? 0.8,
    gap: clamp(c.gap, 0, 0.1),
    margin: clamp(c.gap, 0, 0.1),
    radius: clamp(c.radius, 0, 0.5),
    background: c.background,
    quality,
  };
}
