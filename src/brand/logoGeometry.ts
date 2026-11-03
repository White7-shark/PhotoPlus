/**
 * Photo Plus mark — "Aperture Plus".
 *
 * Six aperture blades close around a hexagonal opening; the opening holds a
 * plus. It reads as a lens (photography), as a shutter in motion, and the
 * plus is the brand's promise: *more* from the phone you already own.
 *
 * Geometry is pure so the same paths render the in-app <Logo/>, the launcher
 * icons (scripts/generate-icons.mjs) and the design preview.
 */

export interface Point {
  x: number;
  y: number;
}

export interface ApertureGeometry {
  size: number;
  center: number;
  outerRadius: number;
  innerRadius: number;
  blades: string[];
  plus: string;
  plusStroke: number;
  bladeGap: number;
}

const round = (n: number) => Math.round(n * 1000) / 1000;
const p = (pt: Point) => `${round(pt.x)} ${round(pt.y)}`;

/** Intersection of ray origin + t·dir (t > 0) with a circle centred at c. */
function rayCircle(origin: Point, dir: Point, c: Point, r: number): Point {
  const ox = origin.x - c.x;
  const oy = origin.y - c.y;
  const b = ox * dir.x + oy * dir.y;
  const cc = ox * ox + oy * oy - r * r;
  const t = -b + Math.sqrt(b * b - cc);
  return { x: origin.x + dir.x * t, y: origin.y + dir.y * t };
}

export function apertureGeometry(size = 100, opening = 0.36, rotationDeg = -15): ApertureGeometry {
  const center = size / 2;
  const c = { x: center, y: center };
  const outerRadius = size * 0.46;
  const innerRadius = outerRadius * opening;
  const n = 6;
  const rot = (rotationDeg * Math.PI) / 180;

  const hex: Point[] = Array.from({ length: n }, (_, k) => {
    const a = rot + (k * 2 * Math.PI) / n;
    return { x: center + innerRadius * Math.cos(a), y: center + innerRadius * Math.sin(a) };
  });

  // Edge k runs hex[k] -> hex[k+1]; extend it past hex[k+1] to the rim.
  const rim: Point[] = hex.map((v, k) => {
    const next = hex[(k + 1) % n];
    const dx = next.x - v.x;
    const dy = next.y - v.y;
    const len = Math.hypot(dx, dy);
    return rayCircle(next, { x: dx / len, y: dy / len }, c, outerRadius);
  });

  const blades = hex.map((_, k) => {
    const pivot = hex[(k + 1) % n];
    const start = rim[k];
    const end = rim[(k + 1) % n];
    // Blade k: pivot -> along edge k to the rim -> arc -> back along edge k+1.
    return `M ${p(pivot)} L ${p(start)} A ${round(outerRadius)} ${round(outerRadius)} 0 0 1 ${p(end)} Z`;
  });

  const arm = innerRadius * 0.52;
  const plus = `M ${round(center - arm)} ${round(center)} H ${round(center + arm)} M ${round(center)} ${round(
    center - arm,
  )} V ${round(center + arm)}`;

  return {
    size,
    center,
    outerRadius,
    innerRadius,
    blades,
    plus,
    plusStroke: innerRadius * 0.24,
    bladeGap: size * 0.018,
  };
}

/** Blade fill stops — the golden-hour → blue-hour spectrum, one hue per blade. */
export const BLADE_COLORS = ['#FF7A45', '#FF4F6E', '#F2477E', '#C04DFF', '#8B5CF6', '#FF9A52'] as const;

/** Full standalone SVG (used for icon generation and previews). */
export function logoSvg(options: { size?: number; background?: boolean; padding?: number } = {}): string {
  const size = options.size ?? 512;
  const pad = options.padding ?? (options.background ? 0.15 : 0);
  const inner = size * (1 - pad * 2);
  const g = apertureGeometry(inner);
  const offset = size * pad;
  const blades = g.blades
    .map(
      (d, i) =>
        `<path d="${d}" fill="url(#blade${i})" stroke="#0A0911" stroke-width="${round(g.bladeGap)}" stroke-linejoin="round"/>`,
    )
    .join('');
  const bladeGradients = BLADE_COLORS.map((color, i) => {
    const next = BLADE_COLORS[(i + 1) % BLADE_COLORS.length];
    return `<linearGradient id="blade${i}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="${color}"/><stop offset="1" stop-color="${next}"/></linearGradient>`;
  }).join('');
  const bg = options.background
    ? `<clipPath id="squircle"><rect width="${size}" height="${size}" rx="${size * 0.225}"/></clipPath>
       <g clip-path="url(#squircle)">
         <rect width="${size}" height="${size}" fill="url(#bg)"/>
         <circle cx="${size * 0.22}" cy="${size * 0.16}" r="${size * 0.55}" fill="url(#glowA)"/>
         <circle cx="${size * 0.88}" cy="${size * 0.9}" r="${size * 0.5}" fill="url(#glowB)"/>
       </g>
       <rect x="${size * 0.004}" y="${size * 0.004}" width="${size * 0.992}" height="${size * 0.992}" rx="${size * 0.222}" fill="none" stroke="rgba(255,255,255,0.14)" stroke-width="${size * 0.006}"/>`
    : '';
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">
  <defs>
    ${bladeGradients}
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1B1430"/><stop offset="1" stop-color="#05050A"/></linearGradient>
    <radialGradient id="glowA"><stop offset="0" stop-color="#FF7A45" stop-opacity="0.35"/><stop offset="1" stop-color="#FF7A45" stop-opacity="0"/></radialGradient>
    <radialGradient id="glowB"><stop offset="0" stop-color="#5B5BF7" stop-opacity="0.35"/><stop offset="1" stop-color="#5B5BF7" stop-opacity="0"/></radialGradient>
    <linearGradient id="sheen" x1="0" y1="0" x2="0.6" y2="1"><stop offset="0" stop-color="#FFFFFF" stop-opacity="0.55"/><stop offset="1" stop-color="#FFFFFF" stop-opacity="0.05"/></linearGradient>
    <radialGradient id="lens" cx="0.4" cy="0.35" r="0.7"><stop offset="0" stop-color="#2A2340"/><stop offset="1" stop-color="#07060C"/></radialGradient>
  </defs>
  ${bg}
  <g transform="translate(${offset} ${offset})">
    <circle cx="${g.center}" cy="${g.center}" r="${round(g.outerRadius + g.bladeGap)}" fill="#0A0911"/>
    ${blades}
    <circle cx="${g.center}" cy="${g.center}" r="${round(g.innerRadius * 0.98)}" fill="url(#lens)"/>
    <path d="M ${round(g.center - g.outerRadius * 0.86)} ${round(g.center - g.outerRadius * 0.12)} A ${round(g.outerRadius * 0.9)} ${round(g.outerRadius * 0.9)} 0 0 1 ${round(g.center + g.outerRadius * 0.62)} ${round(g.center - g.outerRadius * 0.7)} A ${round(g.outerRadius * 1.3)} ${round(g.outerRadius * 1.3)} 0 0 0 ${round(g.center - g.outerRadius * 0.86)} ${round(g.center - g.outerRadius * 0.12)} Z" fill="url(#sheen)"/>
    <path d="${g.plus}" stroke="#FFFFFF" stroke-width="${round(g.plusStroke)}" stroke-linecap="round"/>
    <circle cx="${round(g.center + g.innerRadius * 0.62)}" cy="${round(g.center - g.innerRadius * 0.62)}" r="${round(g.plusStroke * 0.42)}" fill="#F5C66B"/>
  </g>
</svg>`;
}
