#!/usr/bin/env node
/**
 * Renders the artwork of the "Created with PhotoPlus" signature watermark
 * into android/app/src/main/assets/brand/:
 *
 *   mark-camera-lines.png  the hand-drawn camera outline (white; tinted at draw time)
 *   mark-camera-color.png  the glossy lens and the gold swoosh across it (kept in colour)
 *   mark-aperture.png      the aperture that replaces the first "o" of "Photo" (white; tinted)
 *
 * The two camera layers share one canvas, so they overlay exactly. The
 * outline is drawn separately so it can switch between white and charcoal
 * ink, as the rest of the mark does.
 *
 *   node scripts/make-brand-mark.mjs
 */
import { mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = join(root, 'android/app/src/main/assets/brand');
mkdirSync(out, { recursive: true });

// Camera canvas: 320 × 300. The body is tilted 6° anticlockwise, as drawn by hand.
const W = 320;
const H = 300;
const TILT = 'rotate(-6 160 175)';
const LENS = { x: 150, y: 196 };

const lines = `
<g transform="${TILT}" fill="none" stroke="#fff" stroke-linecap="round" stroke-linejoin="round">
  <path stroke-width="10" d="M64,98 L150,98 L164,52 Q168,42 180,42 L262,42 Q274,42 278,54 L284,98 Q294,100 294,116
    L294,268 Q294,290 270,290 L52,290 Q28,290 28,266 L28,128 Q28,98 64,98 Z"/>
  <path stroke-width="8" d="M44,82 L86,77"/>
  <circle cx="${LENS.x}" cy="${LENS.y}" r="80" stroke-width="8"/>
</g>`;

const color = `
<defs>
  <linearGradient id="glass" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#FF3FD2"/><stop offset="0.35" stop-color="#A12CF0"/>
    <stop offset="0.68" stop-color="#2F6BFF"/><stop offset="1" stop-color="#00D2FF"/>
  </linearGradient>
  <radialGradient id="pupil" cx="0.42" cy="0.4" r="0.65">
    <stop offset="0" stop-color="#3A2C5E"/><stop offset="0.55" stop-color="#120C22"/><stop offset="1" stop-color="#05040B"/>
  </radialGradient>
  <radialGradient id="sheen" cx="0.3" cy="0.25" r="0.6">
    <stop offset="0" stop-color="#fff" stop-opacity="0.55"/><stop offset="1" stop-color="#fff" stop-opacity="0"/>
  </radialGradient>
  <linearGradient id="gold" x1="0" y1="1" x2="1" y2="0">
    <stop offset="0" stop-color="#E8891A"/><stop offset="0.45" stop-color="#F6B42C"/><stop offset="1" stop-color="#FFD54F"/>
  </linearGradient>
</defs>
<g transform="${TILT}">
  <circle cx="${LENS.x}" cy="${LENS.y}" r="70" fill="#120A20"/>
  <circle cx="${LENS.x}" cy="${LENS.y}" r="64" fill="url(#glass)"/>
  <circle cx="${LENS.x}" cy="${LENS.y}" r="34" fill="url(#pupil)"/>
  <circle cx="${LENS.x}" cy="${LENS.y}" r="34" fill="none" stroke="#000" stroke-opacity="0.5" stroke-width="4"/>
  <circle cx="${LENS.x}" cy="${LENS.y}" r="13" fill="#05040A"/>
  <circle cx="${LENS.x}" cy="${LENS.y}" r="64" fill="url(#sheen)"/>
  <ellipse cx="${LENS.x - 18}" cy="${LENS.y - 20}" rx="10" ry="7" fill="#fff" fill-opacity="0.92" transform="rotate(-30 ${LENS.x - 18} ${LENS.y - 20})"/>
  <circle cx="${LENS.x + 10}" cy="${LENS.y + 9}" r="3.5" fill="#fff" fill-opacity="0.6"/>
</g>
<path fill="url(#gold)" d="M56,296 Q204,254 318,114 Q184,302 56,296 Z"/>`;

// Aperture: a white disc with a hexagonal opening and six blade edges.
const blades = Array.from({ length: 6 }, (_, i) => {
  const a = (Math.PI / 3) * i - Math.PI / 2;
  const r = 15;
  const x0 = 50 + r * Math.cos(a);
  const y0 = 50 + r * Math.sin(a);
  const b = a + Math.PI / 2 + 0.35;
  return `<path d="M${x0.toFixed(2)},${y0.toFixed(2)} L${(x0 + 40 * Math.cos(b)).toFixed(2)},${(y0 + 40 * Math.sin(b)).toFixed(2)}" stroke="#000" stroke-width="4.2" stroke-linecap="round"/>`;
}).join('');
const hexagon = Array.from({ length: 6 }, (_, i) => {
  const a = (Math.PI / 3) * i - Math.PI / 2;
  return `${(50 + 15 * Math.cos(a)).toFixed(2)},${(50 + 15 * Math.sin(a)).toFixed(2)}`;
}).join(' ');
const aperture = `
<defs><mask id="m"><circle cx="50" cy="50" r="48" fill="#fff"/>
<polygon points="${hexagon}" fill="#000"/>${blades}</mask></defs>
<circle cx="50" cy="50" r="48" fill="#fff" mask="url(#m)"/>`;

const ART = [
  ['mark-camera-lines.png', W, H, lines, 4],
  ['mark-camera-color.png', W, H, color, 4],
  ['mark-aperture.png', 100, 100, aperture, 4],
];

const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 1 });
for (const [file, w, h, svg, scale] of ART) {
  await page.setViewportSize({ width: w * scale, height: h * scale });
  await page.setContent(
    `<html><body style="margin:0;background:transparent"><svg xmlns="http://www.w3.org/2000/svg" width="${w * scale}" height="${h * scale}" viewBox="0 0 ${w} ${h}">${svg}</svg></body></html>`,
  );
  await page.screenshot({ path: join(out, file), omitBackground: true });
  console.log(`${file} ${w * scale}x${h * scale}`);
}
await browser.close();
