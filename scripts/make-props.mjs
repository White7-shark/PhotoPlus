#!/usr/bin/env node
/**
 * Renders the Props artwork (hats, glasses, outfit accessories) to
 * transparent PNGs in android/app/src/main/assets/props/.
 *
 * Every prop is drawn as SVG with lighting gradients and procedural
 * material textures (felt, knit, twill, straw, satin, tortoiseshell, metal)
 * made with SVG turbulence filters, so the art is original, reproducible and
 * renders identically everywhere. The engine then blends each prop into the
 * photo (light, colour, contact shadow, sharpness and grain).
 *
 *   node scripts/make-props.mjs
 *
 * Anchor data (where the prop meets the head, eyes or neck) lives in
 * src/services/edit/props.ts and must match the drawings below.
 */
import { mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = join(root, 'android/app/src/main/assets/props');
mkdirSync(out, { recursive: true });

// ---- shared material filters --------------------------------------------
const defs = `
<filter id="felt" x="-5%" y="-5%" width="110%" height="110%">
  <feTurbulence type="fractalNoise" baseFrequency="0.9" numOctaves="3" seed="7" result="n"/>
  <feColorMatrix in="n" type="matrix" values="0 0 0 0 0.5  0 0 0 0 0.5  0 0 0 0 0.5  0.33 0.33 0.33 0 -0.35" result="g"/>
  <feComposite in="g" in2="SourceGraphic" operator="in" result="t"/>
  <feBlend in="SourceGraphic" in2="t" mode="overlay"/>
</filter>
<filter id="fine" x="-5%" y="-5%" width="110%" height="110%">
  <feTurbulence type="fractalNoise" baseFrequency="1.8" numOctaves="2" seed="3" result="n"/>
  <feColorMatrix in="n" type="matrix" values="0 0 0 0 0.5  0 0 0 0 0.5  0 0 0 0 0.5  0.3 0.3 0.3 0 -0.38" result="g"/>
  <feComposite in="g" in2="SourceGraphic" operator="in" result="t"/>
  <feBlend in="SourceGraphic" in2="t" mode="overlay"/>
</filter>
<filter id="straw" x="-5%" y="-5%" width="110%" height="110%">
  <feTurbulence type="fractalNoise" baseFrequency="0.035 0.9" numOctaves="2" seed="11" result="a"/>
  <feTurbulence type="fractalNoise" baseFrequency="0.9 0.035" numOctaves="2" seed="12" result="b"/>
  <feBlend in="a" in2="b" mode="multiply" result="n"/>
  <feColorMatrix in="n" type="matrix" values="0 0 0 0 0.5  0 0 0 0 0.5  0 0 0 0 0.5  0.5 0.5 0.5 0 -0.45" result="g"/>
  <feComposite in="g" in2="SourceGraphic" operator="in" result="t"/>
  <feBlend in="SourceGraphic" in2="t" mode="overlay"/>
</filter>
<filter id="tortoise" x="0" y="0" width="100%" height="100%">
  <!-- Acetate tortoiseshell: deep brown with honey-amber clouds and darker
       flecks. Noise is stretched to a grey ramp, then mapped through the
       shell's colour ramp. -->
  <feTurbulence type="fractalNoise" baseFrequency="0.028 0.045" numOctaves="5" seed="21" result="n"/>
  <feColorMatrix in="n" type="matrix" values="1.5 1.5 1.5 0 -1.85  1.5 1.5 1.5 0 -1.85  1.5 1.5 1.5 0 -1.85  0 0 0 0 1" result="g"/>
  <feComponentTransfer in="g" result="c">
    <feFuncR type="table" tableValues="0.09 0.11 0.16 0.34 0.62 0.80 0.86"/>
    <feFuncG type="table" tableValues="0.035 0.04 0.06 0.15 0.33 0.50 0.58"/>
    <feFuncB type="table" tableValues="0.015 0.015 0.02 0.04 0.09 0.16 0.22"/>
  </feComponentTransfer>
  <feComposite in="c" in2="SourceGraphic" operator="in"/>
</filter>
<filter id="soft"><feGaussianBlur stdDeviation="6"/></filter>
<filter id="softer"><feGaussianBlur stdDeviation="14"/></filter>
<linearGradient id="gold" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#FFF1B8"/><stop offset="0.28" stop-color="#E9C25A"/><stop offset="0.55" stop-color="#B8862A"/>
  <stop offset="0.78" stop-color="#E7BE58"/><stop offset="1" stop-color="#8C6118"/>
</linearGradient>
<linearGradient id="goldH" x1="0" y1="0" x2="1" y2="0">
  <stop offset="0" stop-color="#8C6118"/><stop offset="0.3" stop-color="#F4D27A"/><stop offset="0.5" stop-color="#FFF4C6"/>
  <stop offset="0.7" stop-color="#D9A845"/><stop offset="1" stop-color="#7E5513"/>
</linearGradient>
<linearGradient id="lensDark" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="#1A1410" stop-opacity="0.92"/><stop offset="0.6" stop-color="#3A2A1C" stop-opacity="0.82"/>
  <stop offset="1" stop-color="#6B4A2E" stop-opacity="0.55"/>
</linearGradient>
<linearGradient id="glint" x1="0" y1="0" x2="1" y2="1">
  <stop offset="0" stop-color="#fff" stop-opacity="0"/><stop offset="0.45" stop-color="#fff" stop-opacity="0.55"/>
  <stop offset="0.55" stop-color="#fff" stop-opacity="0.15"/><stop offset="1" stop-color="#fff" stop-opacity="0"/>
</linearGradient>
<radialGradient id="pearl" cx="0.36" cy="0.32" r="0.75">
  <stop offset="0" stop-color="#FFFFFF"/><stop offset="0.35" stop-color="#F6F0E6"/><stop offset="0.75" stop-color="#D9D0C2"/>
  <stop offset="1" stop-color="#A89F92"/>
</radialGradient>
`;

// ---- the props (viewBox width 1000) -----------------------------------------
const PROPS = {
  // Charcoal felt fedora with a grosgrain band. Head meets the crown base at y 392.
  fedora: {
    h: 560,
    svg: `
<defs>
  <linearGradient id="crownL" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#1C1A1D"/><stop offset="0.35" stop-color="#4A4549"/><stop offset="0.6" stop-color="#3A363A"/><stop offset="1" stop-color="#151316"/></linearGradient>
  <linearGradient id="crownV" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity="0.12"/><stop offset="1" stop-color="#000" stop-opacity="0.25"/></linearGradient>
  <linearGradient id="brimTop" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3F3A3E"/><stop offset="0.7" stop-color="#2A272A"/><stop offset="1" stop-color="#121012"/></linearGradient>
  <linearGradient id="band" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0B0A0B"/><stop offset="0.45" stop-color="#2B272A"/><stop offset="1" stop-color="#070607"/></linearGradient>
</defs>
<g filter="url(#felt)">
  <path d="M30,405 C110,335 300,322 500,322 C700,322 890,335 970,405 C905,455 720,478 500,478 C280,478 95,455 30,405 Z" fill="url(#brimTop)"/>
  <path d="M30,405 C95,455 280,478 500,478 C720,478 905,455 970,405 C960,430 900,470 760,490 C660,503 580,506 500,506 C420,506 340,503 240,490 C100,470 40,430 30,405 Z" fill="#141214"/>
  <ellipse cx="500" cy="395" rx="300" ry="34" fill="#000" opacity="0.55" filter="url(#soft)"/>
  <path d="M262,398 C250,300 268,175 330,130 C392,92 448,124 500,104 C552,124 608,92 670,130 C732,175 750,300 738,398 C650,412 350,412 262,398 Z" fill="url(#crownL)"/>
  <path d="M262,398 C250,300 268,175 330,130 C392,92 448,124 500,104 C552,124 608,92 670,130 C732,175 750,300 738,398 C650,412 350,412 262,398 Z" fill="url(#crownV)"/>
  <path d="M420,118 C450,170 455,260 448,330" stroke="#000" stroke-opacity="0.35" stroke-width="16" fill="none" filter="url(#soft)"/>
  <path d="M580,118 C550,170 545,260 552,330" stroke="#000" stroke-opacity="0.35" stroke-width="16" fill="none" filter="url(#soft)"/>
  <path d="M352,150 C372,135 400,128 425,132" stroke="#fff" stroke-opacity="0.18" stroke-width="10" fill="none" filter="url(#soft)"/>
</g>
<path d="M258,330 C340,342 660,342 742,330 L740,398 C650,412 350,412 260,398 Z" fill="url(#band)"/>
<path d="M258,338 C340,350 660,350 742,338" stroke="#fff" stroke-opacity="0.1" stroke-width="3" fill="none"/>
<path d="M300,338 l-28,-6 l-6,52 l34,4 Z" fill="#141214"/>
<path d="M300,338 c18,6 22,20 18,42 l-18,-2 Z" fill="#1E1B1D"/>`,
  },
  // Ribbed knit beanie with a fold-up cuff. Head meets the cuff bottom at y 520.
  beanie: {
    h: 600,
    svg: `
<defs>
  <pattern id="rib" width="22" height="600" patternUnits="userSpaceOnUse"><rect width="22" height="600" fill="#C9962F"/><rect x="0" width="6" height="600" fill="#9B6F1C"/><rect x="11" width="4" height="600" fill="#E1B452"/></pattern>
  <pattern id="knit" width="26" height="22" patternUnits="userSpaceOnUse"><rect width="26" height="22" fill="#C38E2B"/><path d="M0,0 L13,14 L26,0" stroke="#8F651A" stroke-width="5" fill="none"/><path d="M0,8 L13,22 L26,8" stroke="#E2B655" stroke-width="3" fill="none" opacity="0.7"/></pattern>
  <radialGradient id="dome" cx="0.42" cy="0.25" r="0.8"><stop offset="0" stop-color="#fff" stop-opacity="0.25"/><stop offset="0.55" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.45"/></radialGradient>
  <linearGradient id="cuffShade" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity="0.15"/><stop offset="0.8" stop-color="#000" stop-opacity="0.1"/><stop offset="1" stop-color="#000" stop-opacity="0.45"/></linearGradient>
  <linearGradient id="cuffSide" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#000" stop-opacity="0.5"/><stop offset="0.2" stop-color="#000" stop-opacity="0"/><stop offset="0.8" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.5"/></linearGradient>
</defs>
<g filter="url(#fine)">
  <path d="M175,410 C150,240 300,70 500,62 C700,70 850,240 825,410 Z" fill="url(#knit)"/>
  <path d="M175,410 C150,240 300,70 500,62 C700,70 850,240 825,410 Z" fill="url(#dome)"/>
  <path d="M160,395 C320,375 680,375 840,395 L852,505 C680,540 320,540 148,505 Z" fill="url(#rib)"/>
  <path d="M160,395 C320,375 680,375 840,395 L852,505 C680,540 320,540 148,505 Z" fill="url(#cuffShade)"/>
  <path d="M160,395 C320,375 680,375 840,395 L852,505 C680,540 320,540 148,505 Z" fill="url(#cuffSide)"/>
  <path d="M160,395 C320,375 680,375 840,395" stroke="#5E4210" stroke-opacity="0.6" stroke-width="8" fill="none" filter="url(#soft)"/>
</g>`,
  },
  // Navy cotton twill cap with a curved bill. Head meets the crown base at y 420.
  cap: {
    h: 560,
    svg: `
<defs>
  <pattern id="twill" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(35)"><rect width="8" height="8" fill="#1F2B48"/><rect width="3" height="8" fill="#263457"/></pattern>
  <radialGradient id="capDome" cx="0.4" cy="0.3" r="0.85"><stop offset="0" stop-color="#fff" stop-opacity="0.18"/><stop offset="0.5" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.5"/></radialGradient>
  <linearGradient id="bill" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2A3860"/><stop offset="0.55" stop-color="#1B2542"/><stop offset="1" stop-color="#0D1222"/></linearGradient>
</defs>
<g filter="url(#fine)">
  <path d="M200,425 C185,250 320,95 500,88 C680,95 815,250 800,425 C650,445 350,445 200,425 Z" fill="url(#twill)"/>
  <path d="M200,425 C185,250 320,95 500,88 C680,95 815,250 800,425 C650,445 350,445 200,425 Z" fill="url(#capDome)"/>
  <path d="M500,90 C470,200 465,330 470,438" stroke="#0E1528" stroke-opacity="0.7" stroke-width="5" fill="none"/>
  <path d="M500,90 C380,150 320,280 300,432" stroke="#0E1528" stroke-opacity="0.6" stroke-width="5" fill="none"/>
  <path d="M500,90 C620,150 680,280 700,432" stroke="#0E1528" stroke-opacity="0.6" stroke-width="5" fill="none"/>
  <ellipse cx="500" cy="92" rx="26" ry="14" fill="#1A2440"/>
  <path d="M230,418 C350,400 650,400 770,418 C800,470 720,535 500,540 C280,535 200,470 230,418 Z" fill="url(#bill)"/>
  <path d="M235,422 C350,405 650,405 765,422" stroke="#000" stroke-opacity="0.55" stroke-width="12" fill="none" filter="url(#soft)"/>
  <path d="M300,500 C400,525 600,525 700,500" stroke="#7F8FB8" stroke-opacity="0.35" stroke-width="3" fill="none" stroke-dasharray="10 8"/>
</g>`,
  },
  // Wide-brim straw sun hat with a black ribbon. Head meets the crown base at y 330.
  sunhat: {
    h: 480,
    svg: `
<defs>
  <linearGradient id="strawC" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#A9844F"/><stop offset="0.4" stop-color="#E4C996"/><stop offset="0.65" stop-color="#D2B47C"/><stop offset="1" stop-color="#97733F"/></linearGradient>
  <linearGradient id="strawB" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#D8BC86"/><stop offset="0.8" stop-color="#C4A46D"/><stop offset="1" stop-color="#7E5F30"/></linearGradient>
</defs>
<g filter="url(#straw)">
  <path d="M10,345 C120,262 330,250 500,250 C670,250 880,262 990,345 C910,412 720,440 500,440 C280,440 90,412 10,345 Z" fill="url(#strawB)"/>
  <ellipse cx="500" cy="332" rx="235" ry="30" fill="#4A3618" opacity="0.5" filter="url(#soft)"/>
  <path d="M275,338 C262,240 300,150 380,122 C440,102 560,102 620,122 C700,150 738,240 725,338 C640,350 360,350 275,338 Z" fill="url(#strawC)"/>
</g>
<path d="M272,288 C350,300 650,300 728,288 L726,338 C640,350 360,350 274,338 Z" fill="#121212"/>
<path d="M272,294 C350,306 650,306 728,294" stroke="#fff" stroke-opacity="0.15" stroke-width="3" fill="none"/>`,
  },
  // Gold crown with gems. Head meets the base at y 400.
  crown: {
    h: 470,
    svg: `
<defs>
  <radialGradient id="ruby" cx="0.35" cy="0.3" r="0.7"><stop offset="0" stop-color="#FF9AA8"/><stop offset="0.35" stop-color="#C8102E"/><stop offset="1" stop-color="#4A0010"/></radialGradient>
  <radialGradient id="sapph" cx="0.35" cy="0.3" r="0.7"><stop offset="0" stop-color="#A9C8FF"/><stop offset="0.35" stop-color="#1D4FC4"/><stop offset="1" stop-color="#071A4A"/></radialGradient>
</defs>
<path d="M190,410 L170,150 L320,280 L400,90 L500,240 L600,90 L680,280 L830,150 L810,410 Z" fill="url(#gold)"/>
<path d="M190,410 L170,150 L320,280 L400,90 L500,240 L600,90 L680,280 L830,150 L810,410 Z" fill="none" stroke="#6B4A10" stroke-width="4" stroke-opacity="0.6"/>
<path d="M180,340 C350,360 650,360 820,340 L812,415 C640,440 360,440 188,415 Z" fill="url(#goldH)"/>
<circle cx="170" cy="150" r="22" fill="url(#pearl)"/><circle cx="400" cy="90" r="24" fill="url(#pearl)"/>
<circle cx="600" cy="90" r="24" fill="url(#pearl)"/><circle cx="830" cy="150" r="22" fill="url(#pearl)"/>
<ellipse cx="500" cy="385" rx="34" ry="26" fill="url(#ruby)"/>
<ellipse cx="330" cy="382" rx="24" ry="20" fill="url(#sapph)"/><ellipse cx="670" cy="382" rx="24" ry="20" fill="url(#sapph)"/>
<path d="M230,200 L250,330" stroke="#FFF6D0" stroke-opacity="0.6" stroke-width="6"/>`,
  },
  // Gold laurel wreath: leaves along the band, pointing to the front. Head meets the band at y 300.
  laurel: {
    h: 420,
    svg: (() => {
      // Band: quadratic curve from (170,315) through control (500,240) to (830,315).
      const pt = t => [
        (1 - t) ** 2 * 170 + 2 * (1 - t) * t * 500 + t * t * 830,
        (1 - t) ** 2 * 315 + 2 * (1 - t) * t * 240 + t * t * 315,
      ];
      const leaf = (x, y, deg, len, shade) =>
        `<g transform="translate(${x.toFixed(1)},${y.toFixed(1)}) rotate(${deg.toFixed(1)})"><path d="M0,0 C${len * 0.35},-${len * 0.2} ${len * 0.75},-${len * 0.18} ${len},0 C${len * 0.75},${len * 0.18} ${len * 0.35},${len * 0.2} 0,0 Z" fill="url(#goldH)" opacity="${shade}" stroke="#6B4A10" stroke-width="1.5" stroke-opacity="0.55"/><path d="M2,0 L${len * 0.92},0" stroke="#7E5513" stroke-width="1.6" stroke-opacity="0.55"/></g>`;
      let s = '';
      for (const side of [0, 1]) {
        for (let i = 0; i < 10; i++) {
          const t = side === 0 ? 0.03 + i * 0.045 : 0.97 - i * 0.045;
          const [x, y] = pt(t);
          const [x2, y2] = pt(t + (side === 0 ? 0.01 : -0.01));
          const tangent = (Math.atan2(y2 - y, x2 - x) * 180) / Math.PI;
          const len = 74 - i * 2.5;
          s += leaf(x, y, tangent - 38, len, 0.95);
          s += leaf(x, y, tangent + 38, len * 0.9, 0.85);
        }
      }
      return `<path d="M170,315 Q500,240 830,315" stroke="url(#gold)" stroke-width="10" fill="none"/>${s}`;
    })(),
  },
  // Wool beret, worn tilted. Head meets the band at y 330.
  beret: {
    h: 440,
    svg: `
<defs>
  <radialGradient id="beretTop" cx="0.42" cy="0.3" r="0.85"><stop offset="0" stop-color="#A3262E"/><stop offset="0.6" stop-color="#7D1820"/><stop offset="1" stop-color="#3E0A0F"/></radialGradient>
</defs>
<g filter="url(#felt)">
  <path d="M90,250 C90,140 300,70 520,74 C760,80 930,150 910,250 C890,330 720,345 560,340 C360,336 100,330 90,250 Z" fill="url(#beretTop)"/>
  <path d="M260,300 C380,330 640,332 760,302 L752,345 C640,372 380,370 268,343 Z" fill="#5A1117"/>
  <path d="M520,74 l-6,-28 l12,0 Z" fill="#5A1117"/>
</g>`,
  },
  // Gold aviators with gradient lenses. Lens centres at x 300 and 700, y 200.
  aviators: {
    h: 380,
    svg: `
<path d="M120,150 C120,110 160,95 230,95 C320,95 410,105 420,140 C430,185 410,280 340,305 C270,330 170,310 140,250 C125,220 120,185 120,150 Z" fill="url(#lensDark)"/>
<path d="M880,150 C880,110 840,95 770,95 C680,95 590,105 580,140 C570,185 590,280 660,305 C730,330 830,310 860,250 C875,220 880,185 880,150 Z" fill="url(#lensDark)"/>
<path d="M150,120 C220,105 330,110 380,130 C330,200 260,260 170,280" fill="url(#glint)" opacity="0.6"/>
<path d="M850,120 C780,105 670,110 620,130 C670,200 740,260 830,280" fill="url(#glint)" opacity="0.45"/>
<g fill="none" stroke="url(#gold)" stroke-width="12">
  <path d="M120,150 C120,110 160,95 230,95 C320,95 410,105 420,140 C430,185 410,280 340,305 C270,330 170,310 140,250 C125,220 120,185 120,150 Z"/>
  <path d="M880,150 C880,110 840,95 770,95 C680,95 590,105 580,140 C570,185 590,280 660,305 C730,330 830,310 860,250 C875,220 880,185 880,150 Z"/>
</g>
<path d="M420,130 C470,112 530,112 580,130" fill="none" stroke="url(#goldH)" stroke-width="12"/>
<path d="M412,104 C470,88 530,88 588,104" fill="none" stroke="url(#goldH)" stroke-width="9"/>
<path d="M120,140 L40,130 M880,140 L960,130" stroke="url(#goldH)" stroke-width="10" stroke-linecap="round"/>`,
  },
  // Thin round wire glasses with clear lenses. Lens centres at x 300 and 700, y 190.
  round: {
    h: 360,
    svg: `
<circle cx="300" cy="190" r="140" fill="#DDE8F0" fill-opacity="0.08"/>
<circle cx="700" cy="190" r="140" fill="#DDE8F0" fill-opacity="0.08"/>
<path d="M210,110 C250,80 330,75 370,95 C300,130 240,190 215,260" fill="url(#glint)" opacity="0.5"/>
<path d="M610,110 C650,80 730,75 770,95 C700,130 640,190 615,260" fill="url(#glint)" opacity="0.4"/>
<g fill="none" stroke="#1E1A17" stroke-width="13"><circle cx="300" cy="190" r="140"/><circle cx="700" cy="190" r="140"/></g>
<g fill="none" stroke="url(#goldH)" stroke-width="4" opacity="0.8"><circle cx="300" cy="190" r="134"/><circle cx="700" cy="190" r="134"/></g>
<path d="M440,170 C470,140 530,140 560,170" fill="none" stroke="#1E1A17" stroke-width="12"/>
<path d="M160,175 L60,160 M840,175 L940,160" stroke="#1E1A17" stroke-width="11" stroke-linecap="round"/>`,
  },
  // Glossy black cat-eye sunglasses. Lens centres at x 300 and 700, y 200.
  cateye: {
    h: 380,
    svg: `
<defs><linearGradient id="acetate" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3A3436"/><stop offset="0.3" stop-color="#0E0C0D"/><stop offset="1" stop-color="#050404"/></linearGradient></defs>
<path d="M90,95 C200,80 360,90 440,120 C455,190 420,290 320,305 C210,320 130,270 110,190 C100,150 92,120 90,95 Z" fill="url(#acetate)"/>
<path d="M910,95 C800,80 640,90 560,120 C545,190 580,290 680,305 C790,320 870,270 890,190 C900,150 908,120 910,95 Z" fill="url(#acetate)"/>
<path d="M135,135 C220,122 340,130 405,150 C400,215 375,270 310,278 C230,288 165,250 150,195 Z" fill="#120E0C" fill-opacity="0.9"/>
<path d="M865,135 C780,122 660,130 595,150 C600,215 625,270 690,278 C770,288 835,250 850,195 Z" fill="#120E0C" fill-opacity="0.9"/>
<path d="M165,150 C230,140 320,145 370,160 C330,200 250,240 180,250" fill="url(#glint)" opacity="0.5"/>
<path d="M440,125 C480,108 520,108 560,125 L560,150 C520,138 480,138 440,150 Z" fill="url(#acetate)"/>
<path d="M95,100 C200,86 350,95 430,122" fill="none" stroke="#fff" stroke-opacity="0.25" stroke-width="5"/>
<path d="M95,110 L30,100 M905,110 L970,100" stroke="#0E0C0D" stroke-width="16" stroke-linecap="round"/>`,
  },
  // Square tortoiseshell frames with clear lenses. Lens centres at x 300 and 700, y 190.
  tortoise: {
    h: 360,
    svg: `
<rect x="140" y="80" width="320" height="230" rx="46" fill="#E8EEF2" fill-opacity="0.07"/>
<rect x="540" y="80" width="320" height="230" rx="46" fill="#E8EEF2" fill-opacity="0.07"/>
<g filter="url(#tortoise)"><g fill="none" stroke="#000" stroke-width="30"><rect x="140" y="80" width="320" height="230" rx="46"/><rect x="540" y="80" width="320" height="230" rx="46"/></g>
<path d="M460,150 C490,128 510,128 540,150" fill="none" stroke="#000" stroke-width="26"/>
<path d="M140,140 L45,128 M860,140 L955,128" stroke="#000" stroke-width="22" stroke-linecap="round"/></g>
<path d="M175,100 C250,92 330,95 380,105 C320,160 250,220 190,260" fill="url(#glint)" opacity="0.45"/>
<g fill="none" stroke="#fff" stroke-opacity="0.18" stroke-width="4"><rect x="128" y="68" width="344" height="254" rx="56"/><rect x="528" y="68" width="344" height="254" rx="56"/></g>
<g fill="none" stroke="#FFF6E8" stroke-opacity="0.35" stroke-width="5" stroke-linecap="round"><path d="M180,72 L420,72"/><path d="M580,72 L820,72"/></g>`,
  },
  // Black satin bow tie. Knot centre at x 500, y 160.
  bowtie: {
    h: 320,
    svg: `
<defs>
  <linearGradient id="satinL" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2E2B2E"/><stop offset="0.35" stop-color="#0B0A0B"/><stop offset="0.6" stop-color="#3A363A"/><stop offset="1" stop-color="#060506"/></linearGradient>
  <linearGradient id="satinK" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3B373B"/><stop offset="0.5" stop-color="#121012"/><stop offset="1" stop-color="#050405"/></linearGradient>
</defs>
<path d="M440,130 C360,60 230,40 150,60 C120,120 120,200 150,260 C230,280 360,260 440,190 Z" fill="url(#satinL)"/>
<path d="M560,130 C640,60 770,40 850,60 C880,120 880,200 850,260 C770,280 640,260 560,190 Z" fill="url(#satinL)"/>
<path d="M150,60 C200,120 200,200 150,260 M850,60 C800,120 800,200 850,260" stroke="#000" stroke-opacity="0.45" stroke-width="18" fill="none" filter="url(#soft)"/>
<path d="M200,95 C280,85 360,100 420,140" stroke="#fff" stroke-opacity="0.16" stroke-width="10" fill="none" filter="url(#soft)"/>
<path d="M800,95 C720,85 640,100 580,140" stroke="#fff" stroke-opacity="0.12" stroke-width="10" fill="none" filter="url(#soft)"/>
<rect x="430" y="110" width="140" height="100" rx="30" fill="url(#satinK)"/>
<path d="M450,128 C490,118 520,118 552,128" stroke="#fff" stroke-opacity="0.2" stroke-width="5" fill="none"/>`,
  },
  // Navy silk necktie (knot and upper blade). Knot centre at x 500, y 90.
  necktie: {
    h: 700,
    svg: `
<defs>
  <pattern id="stripe" width="70" height="70" patternUnits="userSpaceOnUse" patternTransform="rotate(40)"><rect width="70" height="70" fill="#1A2A55"/><rect width="16" height="70" fill="#A8873F"/></pattern>
  <linearGradient id="tieShade" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#000" stop-opacity="0.45"/><stop offset="0.35" stop-color="#fff" stop-opacity="0.12"/><stop offset="0.65" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity="0.5"/></linearGradient>
</defs>
<g filter="url(#fine)">
  <path d="M440,150 L560,150 L640,600 L500,690 L360,600 Z" fill="url(#stripe)"/>
  <path d="M440,150 L560,150 L640,600 L500,690 L360,600 Z" fill="url(#tieShade)"/>
  <path d="M420,40 L580,40 L555,150 L445,150 Z" fill="url(#stripe)"/>
  <path d="M420,40 L580,40 L555,150 L445,150 Z" fill="url(#tieShade)"/>
  <path d="M445,150 L555,150" stroke="#000" stroke-opacity="0.55" stroke-width="14" filter="url(#soft)"/>
</g>`,
  },
  // Pearl necklace along a soft curve. Centre front at x 500, y 330.
  pearls: {
    h: 420,
    svg: (() => {
      let s = '';
      const n = 31;
      for (let i = 0; i < n; i++) {
        const t = i / (n - 1);
        const x = 90 + 820 * t;
        const y = 80 + 250 * Math.sin(Math.PI * t) ** 1.4;
        s += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="17" fill="url(#pearl)"/>`;
      }
      return `<g filter="url(#fine)">${s}</g>`;
    })(),
  },
  // Gold curb chain: flat links following the curve. Centre front at x 500, y 300.
  chain: {
    h: 400,
    svg: (() => {
      const curve = t => [110 + 780 * t, 80 + 220 * Math.sin(Math.PI * t) ** 1.3];
      // Equal spacing along the curve.
      const pts = [];
      let prev = curve(0);
      let acc = 0;
      pts.push({ p: prev, t: 0 });
      for (let k = 1; k <= 4000; k++) {
        const t = k / 4000;
        const p = curve(t);
        acc += Math.hypot(p[0] - prev[0], p[1] - prev[1]);
        prev = p;
        if (acc >= 30) {
          pts.push({ p, t });
          acc = 0;
        }
      }
      let s = '';
      pts.forEach(({ p, t }, i) => {
        const q = curve(Math.min(1, t + 0.002));
        const ang = (Math.atan2(q[1] - p[1], q[0] - p[0]) * 180) / Math.PI;
        const tilt = i % 2 ? 1 : -1;
        s += `<g transform="translate(${p[0].toFixed(1)},${p[1].toFixed(1)}) rotate(${ang.toFixed(1)})"><ellipse rx="21" ry="12" transform="skewX(${(tilt * 22).toFixed(0)})" fill="none" stroke="url(#gold)" stroke-width="8"/><path d="M-12,-7 L10,-9" stroke="#FFF4C6" stroke-opacity="0.7" stroke-width="2.5" stroke-linecap="round" transform="skewX(${(tilt * 22).toFixed(0)})"/></g>`;
      });
      return s;
    })(),
  },
};

const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 1 });
for (const [id, { h, svg }] of Object.entries(PROPS)) {
  const W = 1024;
  const H = Math.round((h / 1000) * W);
  await page.setViewportSize({ width: W, height: H });
  await page.setContent(
    `<!doctype html><html><body style="margin:0;background:transparent"><svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 1000 ${h}"><defs>${defs}</defs>${svg}</svg></body></html>`,
  );
  await page.screenshot({ path: join(out, `${id}.png`), omitBackground: true });
  console.log(`${id}.png ${W}x${H}`);
}
await browser.close();
