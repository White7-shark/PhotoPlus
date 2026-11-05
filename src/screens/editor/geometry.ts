/** Pure geometry for the editor's preview: what a tap or a drag means on the photo. */

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

/** Where an image of `cw` x `ch` shows inside a `vw` x `vh` view (contain fit). */
export function containRect(vw: number, vh: number, cw: number, ch: number): Rect {
  if (vw <= 0 || vh <= 0 || cw <= 0 || ch <= 0) return { x: 0, y: 0, w: 0, h: 0 };
  const s = Math.min(vw / cw, vh / ch);
  const w = cw * s;
  const h = ch * s;
  return { x: (vw - w) / 2, y: (vh - h) / 2, w, h };
}

/**
 * A tap in the preview view → a point on the photo (fractions), or null when
 * it lands outside the photo (letterbox, or around a frame's window).
 * `photoRect` is where the photo sits inside the rendered result.
 */
export function viewToPhoto(
  tap: { x: number; y: number },
  view: { width: number; height: number },
  content: { width: number; height: number },
  photoRect: readonly [number, number, number, number] = [0, 0, 1, 1],
): { x: number; y: number } | null {
  const r = containRect(view.width, view.height, content.width, content.height);
  if (r.w === 0) return null;
  const u = (tap.x - r.x) / r.w;
  const v = (tap.y - r.y) / r.h;
  const [px, py, pw, ph] = photoRect;
  const x = (u - px) / pw;
  const y = (v - py) / ph;
  if (x < 0 || y < 0 || x > 1 || y > 1) return null;
  return { x, y };
}

/** The photo's area in view pixels (to draw overlays over it). */
export function photoInView(
  view: { width: number; height: number },
  content: { width: number; height: number },
  photoRect: readonly [number, number, number, number] = [0, 0, 1, 1],
): Rect {
  const r = containRect(view.width, view.height, content.width, content.height);
  const [px, py, pw, ph] = photoRect;
  return { x: r.x + px * r.w, y: r.y + py * r.h, w: pw * r.w, h: ph * r.h };
}

export interface CropAspect {
  id: string;
  label: string;
  /** width / height; null keeps the photo's own shape. */
  ratio: number | null;
}

export const CROP_ASPECTS: readonly CropAspect[] = [
  { id: 'original', label: 'Original', ratio: null },
  { id: 'square', label: 'Square', ratio: 1 },
  { id: 'portrait', label: '4:5', ratio: 4 / 5 },
  { id: 'photo', label: '3:4', ratio: 3 / 4 },
  { id: 'story', label: '9:16', ratio: 9 / 16 },
  { id: 'wide', label: '16:9', ratio: 16 / 9 },
];

/**
 * The largest crop of `ratio` that fits, scaled by `size` (0.3..1) and
 * centred on (cx, cy) as far as the photo allows. Fractions of the photo.
 */
export function aspectCrop(
  ratio: number | null,
  photo: { width: number; height: number },
  size = 1,
  cx = 0.5,
  cy = 0.5,
): Rect {
  const r = ratio ?? photo.width / photo.height;
  const s = Math.min(1, Math.max(0.3, size));
  // Largest rect of ratio r inside the photo, in pixels.
  let w = photo.width;
  let h = w / r;
  if (h > photo.height) {
    h = photo.height;
    w = h * r;
  }
  const fw = (w * s) / photo.width;
  const fh = (h * s) / photo.height;
  const x = Math.min(1 - fw, Math.max(0, cx - fw / 2));
  const y = Math.min(1 - fh, Math.max(0, cy - fh / 2));
  return { x, y, w: fw, h: fh };
}

/** Whether a crop changes anything. */
export function isFullCrop(c: Rect | null): boolean {
  return !c || (c.x <= 0.001 && c.y <= 0.001 && c.w >= 0.999 && c.h >= 0.999);
}
