import React from 'react';
import { Ellipse, G, Line, Polygon } from 'react-native-svg';

/** A landmark for drawing: preview fractions and whether to draw it. */
export interface DrawPoint {
  x: number;
  y: number;
  ok: boolean;
}

const LIMBS: [number, number, number][] = [
  // [from, to, width in shoulder widths]
  [11, 13, 0.32], [13, 15, 0.26], [15, 19, 0.2],
  [12, 14, 0.32], [14, 16, 0.26], [16, 20, 0.2],
  [23, 25, 0.46], [25, 27, 0.36], [27, 31, 0.22],
  [24, 26, 0.46], [26, 28, 0.36], [28, 32, 0.22],
];

/**
 * A soft body shape from pose landmarks (torso, neck, head and limbs), in
 * pixels of a width × height frame. `unit` is one shoulder width as a share
 * of the frame height, so the shape is in proportion whatever the framing.
 * Drawn twice (an outline, then the fill) when `outline` is given.
 */
export function PoseSilhouette({
  points,
  unit,
  width,
  height,
  color,
  opacity = 1,
  outline,
}: {
  points: DrawPoint[];
  unit: number;
  width: number;
  height: number;
  color: string;
  opacity?: number;
  outline?: string;
}) {
  if (points.length < 33 || !(unit > 0)) return null;
  const u = unit * height;
  const P = points.map(p => ({ x: p.x * width, y: p.y * height, ok: p.ok }));
  const layer = (fill: string, grow: number, key: string) => {
    const shapes: React.ReactNode[] = [];
    const sh = [P[11], P[12]];
    if (sh[0].ok && sh[1].ok) {
      // Torso: shoulders a little wider than the joints, down to the hips.
      const dx = sh[0].x - sh[1].x;
      const dy = sh[0].y - sh[1].y;
      const len = Math.hypot(dx, dy) || 1;
      const ox = (dx / len) * (0.16 * u + grow);
      const oy = (dy / len) * (0.16 * u + grow);
      const hipsSeen = P[23].ok && P[24].ok;
      const hl = hipsSeen ? P[23] : { x: sh[0].x - 0.1 * u, y: sh[0].y + 1.3 * u };
      const hr = hipsSeen ? P[24] : { x: sh[1].x + 0.1 * u, y: sh[1].y + 1.3 * u };
      const hx = ((hl.x - hr.x) / (Math.hypot(hl.x - hr.x, hl.y - hr.y) || 1)) * (0.14 * u + grow);
      shapes.push(
        <Polygon
          key={`${key}-torso`}
          points={`${sh[1].x - ox},${sh[1].y - oy} ${sh[0].x + ox},${sh[0].y + oy} ${hl.x + hx},${hl.y} ${hr.x - hx},${hr.y}`}
          fill={fill}
        />,
      );
      if (P[0].ok) {
        const mx = (sh[0].x + sh[1].x) / 2;
        const my = (sh[0].y + sh[1].y) / 2;
        shapes.push(
          <Line
            key={`${key}-neck`}
            x1={mx}
            y1={my}
            x2={P[0].x}
            y2={P[0].y + 0.15 * u}
            stroke={fill}
            strokeWidth={0.34 * u + 2 * grow}
            strokeLinecap="round"
          />,
        );
      }
    }
    if (P[0].ok) {
      // Head: an oval around the face, tilted with the eye line.
      const tilt = P[2].ok && P[5].ok ? (Math.atan2(P[2].y - P[5].y, P[2].x - P[5].x) * 180) / Math.PI : 0;
      const cx = P[0].x;
      const cy = P[0].y - 0.06 * u;
      shapes.push(
        <Ellipse
          key={`${key}-head`}
          cx={cx}
          cy={cy}
          rx={0.27 * u + grow}
          ry={0.36 * u + grow}
          fill={fill}
          transform={`rotate(${tilt} ${cx} ${cy})`}
        />,
      );
    }
    for (const [a, b, w] of LIMBS) {
      if (!P[a].ok || !P[b].ok) continue;
      shapes.push(
        <Line
          key={`${key}-${a}-${b}`}
          x1={P[a].x}
          y1={P[a].y}
          x2={P[b].x}
          y2={P[b].y}
          stroke={fill}
          strokeWidth={w * u + 2 * grow}
          strokeLinecap="round"
        />,
      );
    }
    return shapes;
  };
  return (
    <G opacity={opacity}>
      {outline ? layer(outline, 1.5, 'o') : null}
      {layer(color, 0, 'f')}
    </G>
  );
}
