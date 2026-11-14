/**
 * Placing the person by hand (pure, unit-tested): one finger moves them,
 * two fingers resize (and move) them; the cut-out is drawn where the engine
 * will put it, so what the finger does is what the photo gets.
 */
import { moveOnGround, type Placement } from './scene';

export interface Touch {
  x: number;
  y: number;
}

/** Where the frame is shown on screen. */
export interface Frame {
  x: number;
  y: number;
  w: number;
  h: number;
}

const mid = (t: readonly Touch[]): Touch => ({
  x: t.reduce((s, p) => s + p.x, 0) / t.length,
  y: t.reduce((s, p) => s + p.y, 0) / t.length,
});
const spread = (t: readonly Touch[]) => (t.length < 2 ? 0 : Math.hypot(t[0].x - t[1].x, t[0].y - t[1].y));

/**
 * The placement after a gesture that started at `start` with `from` touches
 * and is now at `now` touches (screen points). Two fingers scale by how far
 * apart they are now against then.
 */
export function applyGesture(
  start: Placement,
  from: readonly Touch[],
  now: readonly Touch[],
  frame: Frame,
  horizon: number,
  followPerspective: boolean,
): Placement {
  if (from.length === 0 || now.length === 0 || frame.w <= 0 || frame.h <= 0) return start;
  const a = mid(from);
  const b = mid(now);
  const dx = (b.x - a.x) / frame.w;
  const dy = (b.y - a.y) / frame.h;
  if (from.length >= 2 && now.length >= 2 && spread(from) > 8) {
    const k = spread(now) / spread(from);
    return { ...start, x: clamp(start.x + dx, -0.2, 1.2), y: clamp(start.y + dy, 0.05, 1.3), height: clamp(start.height * k, 0.08, 1.6) };
  }
  return moveOnGround(start, start.x + dx, start.y + dy, horizon, followPerspective);
}

function clamp(v: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, v));
}

/**
 * Where to draw the whole cut-out image (the photo-sized PNG) on screen so
 * the person's box lands as the engine places it: box bottom centre at the
 * anchor, box height = placement.height of the frame.
 */
export function overlayRect(
  p: Placement,
  box: readonly [number, number, number, number],
  photoAspect: number,
  frame: Frame,
): { left: number; top: number; width: number; height: number } {
  const [l, t, r, b] = box;
  const boxH = Math.max(b - t, 1e-3);
  const height = (p.height * frame.h) / boxH;
  const width = height * photoAspect;
  const ax = frame.x + p.x * frame.w;
  const ay = frame.y + p.y * frame.h;
  const cx = (l + r) / 2;
  // Mirrored: the box centre stays at the anchor.
  const left = p.flip ? ax - (1 - cx) * width : ax - cx * width;
  return { left, top: ay - b * height, width, height };
}

/** A screen point on the shown photo → photo fractions (or null outside it). */
export function toPhoto(x: number, y: number, area: Frame): Touch | null {
  if (area.w <= 0 || area.h <= 0) return null;
  const px = (x - area.x) / area.w;
  const py = (y - area.y) / area.h;
  if (px < -0.02 || px > 1.02 || py < -0.02 || py > 1.02) return null;
  return { x: clamp(px, 0, 1), y: clamp(py, 0, 1) };
}

/**
 * A stroke's points thinned to what matters: consecutive points closer than
 * a fraction of the brush are dropped (a slow finger sends hundreds).
 */
export function thin(points: readonly Touch[], radius: number): number[] {
  const out: number[] = [];
  let last: Touch | null = null;
  const minStep = Math.max(radius * 0.35, 0.002);
  for (const p of points) {
    if (last && Math.hypot(p.x - last.x, p.y - last.y) < minStep) continue;
    out.push(p.x, p.y);
    last = p;
  }
  const end = points.at(-1);
  if (end && last && (end.x !== last.x || end.y !== last.y)) out.push(end.x, end.y);
  return out;
}
