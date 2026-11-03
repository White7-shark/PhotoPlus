#!/usr/bin/env node
/**
 * Generates Android launcher icons and brand exports from the shared logo
 * geometry (src/brand/logoGeometry.ts). Dev-time only; requires Playwright
 * (Chromium) to rasterise SVG. Usage: node scripts/generate-icons.mjs
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

import { logoSvg } from '../src/brand/logoGeometry.ts';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const res = join(root, 'android/app/src/main/res');

const densities = { mdpi: 1, hdpi: 1.5, xhdpi: 2, xxhdpi: 3, xxxhdpi: 4 };

async function rasterise(page, svg, size, out) {
  await page.setViewportSize({ width: size, height: size });
  await page.setContent(`<html><body style="margin:0;background:transparent">${svg.replace(/width="\d+" height="\d+"/, `width="${size}" height="${size}"`)}</body></html>`);
  mkdirSync(dirname(out), { recursive: true });
  await page.screenshot({ path: out, omitBackground: true, clip: { x: 0, y: 0, width: size, height: size } });
}

const browser = await chromium.launch();
const page = await browser.newPage();

const fullIcon = logoSvg({ size: 512, background: true });
const roundIcon = fullIcon.replace(/rx="[\d.]+"/g, 'rx="256"');
// Adaptive foreground: mark only, inside the 66% safe zone of a 108dp canvas.
const foreground = logoSvg({ size: 512, padding: 0.24 });

for (const [name, scale] of Object.entries(densities)) {
  await rasterise(page, fullIcon, Math.round(48 * scale), join(res, `mipmap-${name}/ic_launcher.png`));
  await rasterise(page, roundIcon, Math.round(48 * scale), join(res, `mipmap-${name}/ic_launcher_round.png`));
  await rasterise(page, foreground, Math.round(108 * scale), join(res, `mipmap-${name}/ic_launcher_foreground.png`));
}

// Brand exports for the store listing and documentation.
writeFileSync(join(root, 'assets/brand/photoplus-icon.svg'), fullIcon);
writeFileSync(join(root, 'assets/brand/photoplus-mark.svg'), logoSvg({ size: 512 }));
await rasterise(page, fullIcon, 512, join(root, 'assets/brand/photoplus-icon-512.png'));
await browser.close();
console.log('Icons generated.');
