#!/usr/bin/env node
/**
 * Renders the Photo Plus design preview (docs/design/*.png) in Chromium using
 * the app's own design inputs: theme token values, the CSS gradient strings
 * the RN components use for `backgroundImage`, the shared logo geometry and
 * the icon paths parsed straight out of src/components/Icon.tsx.
 *
 * Glass is built exactly as in the app — translucent fill + sheen + hairline
 * over a blurred aurora — with no browser-only backdrop-filter.
 */
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { logoSvg, apertureGeometry, BLADE_COLORS } from '../src/brand/logoGeometry.ts';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = join(root, 'docs/design');
mkdirSync(out, { recursive: true });

// ---- tokens (mirrors src/theme/tokens.ts; values copied verbatim) ----------
const tokensSrc = readFileSync(join(root, 'src/theme/tokens.ts'), 'utf8');
const gradientsBlock = tokensSrc.slice(tokensSrc.indexOf('export const gradients = {'));
const grad = name => gradientsBlock.match(new RegExp(`\\n  ${name}:\\s*\\n?\\s*'([^']+)'`))[1];
const G = {
  spectrum: grad('spectrum'),
  action: grad('action'),
  premium: grad('premium'),
  lagoon: grad('lagoon'),
  glassSheen: grad('glassSheen'),
  scrim: grad('scrim'),
};

// ---- icons parsed from Icon.tsx -------------------------------------------
const iconSrc = readFileSync(join(root, 'src/components/Icon.tsx'), 'utf8');
const body = iconSrc.slice(iconSrc.indexOf('export const ICONS = {'), iconSrc.indexOf('} as const;'));
const ICONS = {};
for (const m of body.matchAll(/\n  (\w+): ([\s\S]*?)(?=,\n  \w+: |,\n$)/g)) {
  ICONS[m[1]] = m[2]
    .replace(/<\/?>/g, '')
    .replace(/[()]/g, s => s) // keep transform parentheses
    .replace(/<Path/g, '<path')
    .replace(/<Circle/g, '<circle')
    .replace(/<Rect/g, '<rect')
    .replace(/=\{([^}]+)\}/g, '="$1"')
    .replace(/^\s*\(\s*|\s*\)\s*$/g, '');
}
const icon = (name, size = 22, color = '#F7F5FB', sw = 1.75) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round">${ICONS[name]}</svg>`;

// ---- in-app logo mark (same construction as Logo.tsx) ---------------------
function mark(size) {
  return logoSvg({ size }).replace(/id="(\w+)"/g, `id="$1_${size}"`).replace(/url\(#(\w+)\)/g, `url(#$1_${size})`);
}

const css = `
:root{--bg:#05050A;--raised:#0A0911;--t1:#F7F5FB;--t2:rgba(247,245,251,0.74);--t3:rgba(247,245,251,0.56);
--accent:#FF7A45;--rose:#F2477E;--lagoon:#2DD4BF;--gold:#F5C66B;--success:#3DDC97;--warning:#FFC145;--danger:#FF6B6B;--info:#6CB4FF;
--glass:rgba(255,255,255,0.065);--glass2:rgba(255,255,255,0.11);--stroke:rgba(255,255,255,0.14);--hl:rgba(255,255,255,0.28);--divider:rgba(255,255,255,0.08)}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Roboto,'Segoe UI',system-ui,sans-serif;background:#0d0c12;color:var(--t1);-webkit-font-smoothing:antialiased}
.phone{position:relative;width:390px;height:844px;overflow:hidden;background:var(--bg);border-radius:44px}
.aurora{position:absolute;inset:0;overflow:hidden;background:var(--bg)}
.blob{position:absolute;border-radius:50%;filter:blur(40px)}
.vignette{position:absolute;inset:0;background-image:radial-gradient(ellipse at center, rgba(5,5,10,0) 40%, rgba(5,5,10,0.55) 100%)}
.content{position:absolute;inset:0;padding:58px 16px 0;overflow:hidden}
.status{position:absolute;top:0;left:0;right:0;height:44px;display:flex;justify-content:space-between;align-items:center;padding:0 26px;font-size:13px;font-weight:600;z-index:5}
.glass{position:relative;background-color:var(--glass);border:1px solid var(--stroke);background-image:${G.glassSheen};box-shadow:0 18px 40px -12px rgba(0,0,0,0.45);overflow:hidden}
.glass::before{content:'';position:absolute;top:0;left:14%;right:14%;height:1px;background-image:linear-gradient(90deg, rgba(255,255,255,0) 0%, var(--hl) 50%, rgba(255,255,255,0) 100%)}
.strong{background-color:var(--glass2)}
.r-md{border-radius:18px}.r-lg{border-radius:24px}.r-xl{border-radius:32px}
.overline{font-size:11px;line-height:14px;font-weight:700;letter-spacing:1.6px;text-transform:uppercase}
.display{font-size:36px;line-height:40px;font-weight:800;letter-spacing:-1.2px}
.title1{font-size:30px;line-height:36px;font-weight:800;letter-spacing:-0.8px}
.title2{font-size:22px;line-height:28px;font-weight:700;letter-spacing:-0.4px}
.title3{font-size:18px;line-height:24px;font-weight:700;letter-spacing:-0.2px}
.body{font-size:15px;line-height:22px}.strongt{font-size:15px;line-height:22px;font-weight:600}
.callout{font-size:13px;line-height:18px;font-weight:500}.caption{font-size:12px;line-height:16px;font-weight:500;letter-spacing:.2px}
.t2{color:var(--t2)}.t3{color:var(--t3)}.acc{color:var(--accent)}
.row{display:flex;align-items:center}.gap8{gap:8px}.gap10{gap:10px}.gap12{gap:12px}
.btn{display:inline-flex;align-items:center;gap:8px;height:52px;padding:0 24px;border-radius:999px;font-weight:700;font-size:15px;letter-spacing:.2px;white-space:nowrap}
.btn.primary{background-image:${G.action};box-shadow:0 10px 28px -10px #F2477E;color:#fff}
.btn.glassb{background-color:var(--glass2);border:1px solid var(--stroke);background-image:${G.glassSheen}}
.btn.premium{background-image:${G.premium};color:#1A1206}
.btn.ghost{color:var(--accent);background:none;padding:0 12px}
.badge{align-self:flex-start;display:inline-flex;align-items:center;gap:4px;padding:4px 9px;border-radius:999px;font-size:12px;font-weight:700;border:1px solid}
.b-acc{background:rgba(255,122,69,.14);color:var(--accent);border-color:rgba(255,122,69,.4)}
.b-gold{background:rgba(245,198,107,.16);color:var(--gold);border-color:rgba(245,198,107,.45)}
.b-ok{background:rgba(61,220,151,.14);color:var(--success);border-color:rgba(61,220,151,.4)}
.b-warn{background:rgba(255,193,69,.14);color:var(--warning);border-color:rgba(255,193,69,.4)}
.b-n{background:var(--glass2);color:var(--t2);border-color:var(--stroke)}
.iconbtn{width:44px;height:44px;border-radius:50%;display:flex;align-items:center;justify-content:center;background-color:var(--glass);border:1px solid var(--stroke);background-image:${G.glassSheen}}
.tile{width:42px;height:42px;border-radius:14px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.14);border:1px solid rgba(255,255,255,.22)}
.tabbar{position:absolute;left:16px;right:16px;bottom:22px;height:68px;border-radius:32px;display:flex;align-items:center;padding:0 6px;z-index:4;background-color:rgba(16,14,25,0.86)!important}
.tab{flex:1;display:flex;flex-direction:column;align-items:center;gap:2px;font-size:11px;color:var(--t3);font-weight:500}
.tab.on{color:var(--t1);font-weight:700}.dot{width:4px;height:4px;border-radius:2px;margin-top:1px}
.tab.on .dot{background:var(--accent)}
.shutterTab{width:58px;height:58px;border-radius:29px;padding:3px;margin-top:-26px;background-image:${G.spectrum};box-shadow:0 8px 24px -6px #F2477E}
.shutterTab>div{width:100%;height:100%;border-radius:26px;border:2px solid rgba(255,255,255,.55);display:flex;align-items:center;justify-content:center}
.lrow{display:flex;align-items:center;gap:12px;padding:12px 16px;border-bottom:1px solid var(--divider)}.lrow:last-child{border-bottom:none}
.ricon{width:34px;height:34px;border-radius:10px;display:flex;align-items:center;justify-content:center;background:var(--glass2)}
.seg{display:flex;padding:4px;border-radius:999px;background:var(--glass);border:1px solid var(--stroke)}
.seg>div{flex:1;height:40px;border-radius:999px;display:flex;align-items:center;justify-content:center;gap:6px;font-size:13px;font-weight:700;color:var(--t2)}
.seg>.sel{background-image:${G.action};color:#fff}
.switch{width:44px;height:26px;border-radius:13px;background:var(--accent);position:relative}.switch::after{content:'';position:absolute;right:3px;top:3px;width:20px;height:20px;border-radius:10px;background:#fff}
.switch.off{background:var(--glass2)}.switch.off::after{left:3px;right:auto}
.bar{height:8px;border-radius:4px;background:var(--glass2);overflow:hidden}.bar>div{height:100%;border-radius:4px;background-image:${G.spectrum}}
`;

function aurora(variant = 'default') {
  const blobs = variant === 'calm'
    ? [['#C04DFF', 420, 'top:-160px;right:-140px', .35], ['#5B5BF7', 380, 'bottom:-160px;left:-120px', .3]]
    : [['#FF7A45', 460, 'top:-170px;left:-150px', .55], ['#C04DFF', 400, 'top:30%;right:-190px', .42], ['#5B5BF7', 480, 'bottom:-220px;left:10%', .45]];
  return `<div class="aurora">${blobs.map(([c, s, pos, o]) => `<div class="blob" style="width:${s}px;height:${s}px;${pos};opacity:${o};background-image:radial-gradient(circle at center, ${c} 0%, ${c} 18%, transparent 70%)"></div>`).join('')}<div class="vignette"></div></div>`;
}
const status = (light = false) => `<div class="status" style="color:${light ? '#fff' : 'var(--t1)'}"><span>9:41</span><span style="display:flex;gap:6px;align-items:center"><svg width="16" height="11" viewBox="0 0 16 11" fill="currentColor"><rect x="0" y="7" width="3" height="4" rx="1"/><rect x="4.3" y="5" width="3" height="6" rx="1"/><rect x="8.6" y="2.5" width="3" height="8.5" rx="1"/><rect x="13" y="0" width="3" height="11" rx="1"/></svg><svg width="24" height="12" viewBox="0 0 24 12" fill="none" stroke="currentColor"><rect x=".5" y=".5" width="20" height="11" rx="3"/><rect x="2" y="2" width="14" height="8" rx="1.5" fill="currentColor"/><path d="M22.5 4v4"/></svg></span></div>`;
const tabbar = active => `<div class="tabbar glass strong r-xl">${[['Studio', 'aperture'], ['Projects', 'folder'], ['Camera'], ['Packs', 'bag'], ['Settings', 'gear']].map(([n, i]) => n === 'Camera' ? `<div class="tab"><div class="shutterTab"><div>${icon('camera', 24, '#fff', 2)}</div></div></div>` : `<div class="tab ${n === active ? 'on' : ''}">${icon(i, 22, n === active ? '#F7F5FB' : 'rgba(247,245,251,0.56)', n === active ? 2.1 : 1.75)}<span>${n}</span><div class="dot"></div></div>`).join('')}</div>`;
const wordmark = size => `<div class="row" style="gap:${size * .28}px">${mark(size)}<div style="display:flex;align-items:baseline"><span style="font-size:${size * .62}px;font-weight:800;letter-spacing:-.6px">Photo</span><span style="font-size:${size * .62}px;font-weight:300;letter-spacing:-.4px;margin-left:${size * .12}px;color:var(--accent)">Plus</span></div></div>`;

const pillars = [
  ['Camera Studio', 'Manual control, pro modes, composition guides', 'aperture', 'rgba(255,122,69,0.55)', false],
  ['AI Assistant', 'Pose matching, live guidance, light analysis', 'sparkles', 'rgba(192,77,255,0.55)', true],
  ['Replication Studio', 'Recreate any shoot: backdrop, light, style', 'layers', 'rgba(91,91,247,0.6)', true],
  ['Editing Studio', 'Retouch, grade, mask, brand and export', 'sliders', 'rgba(45,212,191,0.5)', false],
];
const premiumBadge = `<span class="badge b-gold">${icon('crown', 12, '#F5C66B', 2.2)}Premium</span>`;

// ---------------------------------------------------------------- screens --
const screens = {};

screens.onboarding = `${aurora()}${status()}<div class="content" style="padding-top:84px;display:flex;flex-direction:column">
  <div style="display:flex;flex-direction:column;align-items:center">${mark(112)}<div class="display" style="margin-top:22px">Photo Plus</div>
  <div class="body t2" style="text-align:center;margin-top:8px;max-width:300px">Your photoshoot studio — camera, director and darkroom in one.</div></div>
  <div style="display:flex;flex-direction:column;gap:12px;margin-top:34px">${[['aperture', 'A pro camera in your pocket', 'Manual controls and composition guides, tuned to your phone.'], ['sparkles', 'An assistant that directs', 'Pose, light and framing guidance — on-device, no cloud.'], ['shield', 'Private by design', 'Works offline. Your photos never leave this device unless you share them.']].map(([i, t, b]) => `<div class="glass r-lg row gap12" style="padding:16px;gap:14px"><div style="width:44px;height:44px;border-radius:14px;display:flex;align-items:center;justify-content:center;background-image:${G.spectrum};flex:none">${icon(i, 22, '#fff')}</div><div><div class="strongt">${t}</div><div class="caption t2" style="margin-top:2px">${b}</div></div></div>`).join('')}</div>
  <div style="flex:1"></div>
  <div class="btn primary" style="justify-content:center;width:100%;height:56px">${icon('camera', 20, '#fff', 2)}Get started</div>
  <div class="btn ghost" style="justify-content:center;width:100%;margin:4px 0 26px">Explore first</div></div>`;

screens.studio = `${aurora()}${status()}<div class="content">
  <div class="row" style="margin-bottom:24px"><div style="flex:1">${wordmark(34)}</div><div class="iconbtn">${icon('cpu', 19)}</div></div>
  <div class="overline acc">Golden hour</div><div class="display" style="margin:6px 0 20px">The best light of the day is here.</div>
  <div class="glass r-xl" style="box-shadow:0 18px 40px -12px rgba(0,0,0,.45),0 0 32px -6px rgba(242,71,126,0.35);margin-bottom:24px"><div style="padding:20px;background-image:linear-gradient(120deg, rgba(255,122,69,0.30) 0%, rgba(242,71,126,0.18) 45%, rgba(139,92,246,0.10) 100%)">
   <span class="badge b-acc">${icon('sparkles', 12, '#FF7A45', 2.2)}New shoot</span><div class="title2" style="margin-top:12px">Start a photoshoot</div>
   <div class="body t2" style="margin:6px 0 16px">Open the camera with composition guides. Every shot is saved privately on this device.</div>
   <div class="row gap10" style="flex-wrap:wrap"><div class="btn primary" style="height:56px">${icon('camera', 20, '#fff', 2)}Open camera</div><div class="btn glassb" style="height:48px;padding:0 20px;font-size:13px">${icon('wand', 18)}Engine lab</div></div></div></div>
  <div class="overline t3">The four pillars</div><div class="title3" style="margin:2px 0 12px">Your studio</div>
  <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">${pillars.map(([t, d, i, c, p]) => `<div class="glass r-lg" style="height:166px"><div style="height:100%;padding:16px;display:flex;flex-direction:column;background-image:linear-gradient(140deg, ${c} 0%, rgba(0,0,0,0) 70%)"><div class="row" style="justify-content:space-between;align-items:flex-start"><div class="tile">${icon(i, 22, '#fff')}</div>${p ? premiumBadge : ''}</div><div style="flex:1"></div><div class="strongt">${t}</div><div class="caption t2" style="margin-top:4px">${d}</div></div></div>`).join('')}</div>
</div>${tabbar('Studio')}`;

// A photographic-looking "viewfinder" scene (no live camera in a browser).
const scene = `<div style="position:absolute;inset:0;background-image:linear-gradient(180deg,#1d1640 0%,#6b2c6e 38%,#ff7d5a 62%,#ffc58a 70%,#2a1a2b 71%,#120c16 100%)"></div>
<div style="position:absolute;left:-40px;right:-40px;top:430px;height:200px;border-radius:50%;background:radial-gradient(ellipse at center, rgba(255,214,150,.75) 0%, rgba(255,140,90,0) 70%);filter:blur(10px)"></div>
<div style="position:absolute;left:228px;top:390px;width:110px;height:110px;border-radius:50%;background:radial-gradient(circle,#fff6d6 0%,#ffd28a 45%,rgba(255,170,90,0) 72%)"></div>
<svg style="position:absolute;left:0;bottom:120px" width="390" height="330" viewBox="0 0 390 330"><path d="M0 230 Q60 170 120 205 T250 190 T390 170 V330 H0Z" fill="#160d1a"/><path d="M118 330 L118 175 Q118 140 145 132 Q160 100 150 82 Q148 60 165 52 Q184 46 192 64 Q198 82 188 100 Q182 120 200 132 Q228 140 228 175 L228 330Z" fill="#0b0710"/></svg>`;
screens.camera = `<div style="position:absolute;inset:0;background:#000">${scene}</div>${status(true)}
  <div style="position:absolute;inset:0">${[1, 2].map(i => `<div style="position:absolute;left:${i * 33.333}%;top:0;bottom:0;width:1px;background:rgba(255,255,255,.35)"></div><div style="position:absolute;top:${i * 33.333}%;left:0;right:0;height:1px;background:rgba(255,255,255,.35)"></div>`).join('')}</div>
  <div class="row" style="position:absolute;top:54px;left:16px;right:16px;justify-content:space-between"><div class="iconbtn" style="width:48px;height:48px">${icon('close', 21)}</div><div class="row gap10"><div class="iconbtn" style="width:48px;height:48px;background-color:rgba(255,255,255,.22)">${icon('flash', 21, '#F5C66B')}</div><div class="iconbtn" style="width:48px;height:48px;background-color:rgba(255,255,255,.22)">${icon('grid', 21)}</div></div></div>
  <div style="position:absolute;top:122px;left:0;right:0;display:flex;justify-content:center"><span class="badge b-ok">${icon('check', 12, '#3DDC97', 2.2)}Saved to Captures</span></div>
  <div style="position:absolute;left:16px;right:16px;bottom:30px;display:flex;flex-direction:column;align-items:center">
   <div class="glass strong row" style="border-radius:36px;width:100%;justify-content:space-between;padding:14px 18px">
    <div style="width:56px;display:flex;justify-content:center"><div style="width:48px;height:48px;border-radius:14px;border:1.5px solid rgba(255,255,255,.6);background-image:linear-gradient(180deg,#6b2c6e,#ff7d5a 60%,#120c16)"></div></div>
    <div style="width:82px;height:82px;border-radius:41px;border:4px solid #fff;display:flex;align-items:center;justify-content:center"><div style="width:64px;height:64px;border-radius:32px;background:#fff"></div></div>
    <div style="width:56px;display:flex;justify-content:center"><div class="iconbtn" style="width:48px;height:48px">${icon('flip', 21)}</div></div></div>
   <div class="caption" style="color:rgba(255,255,255,.75);margin-top:10px;letter-spacing:1.4px">PHOTO · BACK CAMERA</div></div>`;

const projects = [['Studio portraits — March', 24, 'linear-gradient(160deg,#FFB36B,#FF5E7E 55%,#5A2A6E)'], ['Captures', 61, 'linear-gradient(160deg,#2DD4BF,#5B5BF7 60%,#120A2A)'], ['Accra street — golden hour', 37, 'linear-gradient(160deg,#F5C66B,#C9922E 50%,#3a2412)'], ['Wedding: Ama & Kofi', 112, 'linear-gradient(160deg,#FFF3E2,#E8C9A8 50%,#8E6B55)']];
screens.projects = `${aurora()}${status()}<div class="content">
  <div class="row" style="margin-bottom:20px"><div style="flex:1"><div class="overline acc">Library</div><div class="title1">Projects</div></div><div class="iconbtn" style="width:48px;height:48px">${icon('plus', 21)}</div></div>
  <div style="display:flex;flex-direction:column;gap:12px">${projects.map(([n, c, g]) => `<div class="glass r-lg row" style="gap:14px;padding:12px"><div style="width:64px;height:64px;border-radius:14px;background-image:${g};flex:none"></div><div style="flex:1;min-width:0"><div class="strongt" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis">${n}</div><div class="caption t2">${c} photos · 3 Nov 2026</div></div><div class="iconbtn">${icon('trash', 19, '#FF6B6B')}</div></div>`).join('')}</div>
</div>${tabbar('Projects')}`;

const packs = [['Golden Coast', 'Warm beach editorial', 24, 'linear-gradient(160deg, #FFB36B 0%, #FF5E7E 55%, #5A2A6E 100%)', true], ['Neon Noir', 'Cinematic city night', 18, 'linear-gradient(160deg, #2DD4BF 0%, #5B5BF7 50%, #120A2A 100%)', false], ['Studio Ivory', 'Clean high-key portraits', 30, 'linear-gradient(160deg, #FFF3E2 0%, #E8C9A8 50%, #8E6B55 100%)', true], ['Kente Royale', 'Bold heritage colour', 20, 'linear-gradient(160deg, #F5C66B 0%, #2F9E5B 45%, #B4232F 100%)', false]];
screens.packs = `${aurora()}${status()}<div class="content">
  <div class="overline acc">Marketplace</div><div class="title1" style="margin-bottom:18px">Photoshoot Packs</div>
  <div class="glass r-xl" style="margin-bottom:20px;box-shadow:0 18px 40px -12px rgba(0,0,0,.45),0 0 32px -6px rgba(245,198,107,0.25)"><div style="padding:18px;background-image:linear-gradient(130deg, rgba(245,198,107,0.28) 0%, rgba(245,198,107,0.04) 70%);display:flex;flex-direction:column;gap:9px">${premiumBadge}<div class="title2">Recreate iconic shoots</div><div class="callout t2">Backdrops, lighting recipes, poses and styling for your camera. Packs are part of Premium — GHS 50/month.</div><div class="row gap8 callout" style="color:var(--gold);font-weight:700;margin-top:2px">${icon('crown', 16, '#F5C66B')}Premium subscriptions arrive in a later update</div></div></div>
  <div class="row" style="margin-bottom:12px;align-items:flex-end"><div style="flex:1"><div class="overline t3">Preview</div><div class="title3">Featured packs</div></div><span class="badge b-n">Coming soon</span></div>
  <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">${packs.map(([t, m, s, g, inc]) => `<div class="glass r-lg"><div style="aspect-ratio:.8;background-image:${g};display:flex;flex-direction:column;justify-content:space-between;padding:12px"><div class="row" style="justify-content:space-between"><span class="badge" style="background:rgba(10,9,17,0.62);color:#fff;border-color:rgba(255,255,255,0.22)">${icon(inc ? 'crown' : 'plus', 12, '#fff', 2.2)}${inc ? 'Included' : 'Add-on'}</span><div style="width:30px;height:30px;border-radius:15px;background:rgba(0,0,0,.35);display:flex;align-items:center;justify-content:center">${icon('lock', 15, '#fff')}</div></div><div style="margin:-12px;padding:30px 12px 12px;background-image:${G.scrim}"><div class="strongt" style="color:#fff">${t}</div><div class="caption" style="color:rgba(255,255,255,.82)">${m} · ${s} looks</div></div></div></div>`).join('')}</div>
</div>${tabbar('Packs')}`;

screens.settings = `${aurora()}${status()}<div class="content">
  <div class="overline acc">Preferences</div><div class="title1" style="margin-bottom:18px">Settings</div>
  <div class="title3" style="margin-bottom:10px">Appearance</div>
  <div class="glass r-lg" style="padding:16px;margin-bottom:20px"><div class="seg"><div class="sel">${icon('moon', 16, '#fff')}Dark</div><div>${icon('sun', 16, 'rgba(247,245,251,0.74)')}Light</div><div>${icon('refresh', 16, 'rgba(247,245,251,0.74)')}Auto</div></div><div class="caption t3" style="margin-top:12px">Dark is recommended for judging exposure and colour accurately.</div></div>
  <div class="title3" style="margin-bottom:10px">Camera &amp; motion</div>
  <div class="glass r-lg" style="margin-bottom:20px"><div class="lrow"><div class="ricon">${icon('grid', 18)}</div><div style="flex:1"><div class="strongt">Composition grid</div><div class="caption t2">Rule-of-thirds overlay in the camera</div></div><div class="switch"></div></div><div class="lrow"><div class="ricon">${icon('sparkles', 18)}</div><div style="flex:1" class="strongt">Haptic feedback</div><div class="switch"></div></div></div>
  <div class="title3" style="margin-bottom:10px">Storage</div>
  <div class="glass r-lg"><div style="padding:16px;display:flex;flex-direction:column;gap:10px"><div class="row" style="justify-content:space-between"><div class="strongt">Photo Plus uses 1.4 GB</div><div class="callout t2">38.6 GB free</div></div><div class="bar"><div style="width:62%"></div></div></div>
  <div class="lrow"><div class="ricon">${icon('image', 18)}</div><div style="flex:1" class="strongt">Captures</div><div class="callout t2">1.1 GB</div></div>
  <div class="lrow"><div class="ricon">${icon('trash', 18, '#FF6B6B')}</div><div style="flex:1" class="strongt">Clear temporary cache</div><div class="callout t2">84 MB</div>${icon('chevronRight', 18, 'rgba(247,245,251,0.56)')}</div></div>
</div>${tabbar('Settings')}`;

const stat = (i, v, l, c) => `<div class="glass r-md" style="flex:1;padding:14px;display:flex;flex-direction:column;gap:10px">${icon(i, 18, c)}<div><div class="title3">${v}</div><div class="caption t3">${l}</div></div></div>`;
screens.device = `${aurora()}${status()}<div class="content">
  <div class="row gap12" style="margin-bottom:16px"><div class="iconbtn">${icon('chevronLeft', 19)}</div><div><div class="overline acc">Tecno Camon 30</div><div class="title2">Device</div></div></div>
  <div class="row gap8" style="margin-bottom:14px;flex-wrap:wrap"><span class="badge b-ok">${icon('shield', 12, '#3DDC97', 2.2)}Performance tier</span><span class="badge b-n">Android 14 · API 34</span></div>
  <div class="row gap10" style="margin-bottom:10px">${stat('memory', '7.4 GB', 'RAM', '#FF7A45')}${stat('storage', '40.0 GB', 'Free storage', '#2DD4BF')}</div>
  <div class="row gap10" style="margin-bottom:20px">${stat('cpu', '8', 'CPU cores', '#6CB4FF')}${stat('camera', '2', 'Cameras', '#F5C66B')}</div>
  <div class="overline t3">Main sensor · full</div><div class="title3" style="margin:2px 0 10px">Camera controls</div>
  <div class="glass r-lg">${[['Manual shutter speed', 'Supported'], ['Manual ISO', '50–6400'], ['RAW capture', 'Supported'], ['Zoom', '1× – 10×']].map(([a, b]) => `<div class="lrow"><div style="flex:1" class="strongt">${a}</div><div class="callout t2">${b}</div></div>`).join('')}</div>
  <div class="overline t3" style="margin-top:18px">Planned for later phases</div><div class="title3" style="margin:2px 0 10px">On-device AI models</div>
  <div class="glass r-lg">${[['Subject segmentation (HQ)', 'Background removal · GPU', true], ['Garment transfer', 'Clothing transformation · GPU', true]].map(([a, b, ok]) => `<div class="lrow"><div style="flex:1"><div class="strongt">${a}</div><div class="caption t2">${b}</div></div><span class="badge ${ok ? 'b-ok' : 'b-warn'}">${ok ? 'Ready' : 'Limited'}</span></div>`).join('')}</div>
</div>`;

// ------------------------------------------------------------- brand sheet --
const sw = (n, c, t = '#fff') => `<div style="flex:1;border-radius:18px;height:110px;padding:12px;display:flex;flex-direction:column;justify-content:flex-end;background:${c};color:${t}"><div style="font-weight:700;font-size:13px">${n}</div><div style="font-size:11px;opacity:.8;font-family:monospace">${c.startsWith('linear') ? 'gradient' : c}</div></div>`;
const brand = `<div style="width:1200px;padding:56px;background:#07060c;position:relative;overflow:hidden">
 <div class="blob" style="width:600px;height:600px;top:-260px;left:-200px;opacity:.5;background-image:radial-gradient(circle,#FF7A45 0%,#FF7A45 18%,transparent 70%)"></div>
 <div class="blob" style="width:600px;height:600px;bottom:-300px;right:-200px;opacity:.45;background-image:radial-gradient(circle,#5B5BF7 0%,#5B5BF7 18%,transparent 70%)"></div>
 <div style="position:relative">
 <div class="overline acc">Photo Plus · Brand</div><div class="title1" style="font-size:40px;line-height:46px;margin:6px 0 6px">Aperture Plus</div>
 <div class="body t2" style="max-width:720px">Six aperture blades in the golden-hour → blue-hour spectrum close around a lens holding a plus: photography, a shutter in motion, and the promise of <i>more</i> from the phone you already own. A glass sheen ties the mark to the app's glass UI.</div>
 <div class="row" style="gap:40px;margin-top:36px;align-items:flex-end">
  <div style="text-align:center">${logoSvg({ size: 260, background: true }).replace(/id="(\w+)"/g, 'id="$1_icon"').replace(/url\(#(\w+)\)/g, 'url(#$1_icon)')}<div class="caption t3" style="margin-top:10px">App icon</div></div>
  <div style="text-align:center"><div style="width:180px;height:180px;border-radius:50%;overflow:hidden">${logoSvg({ size: 180, background: true }).replace(/id="(\w+)"/g, 'id="$1_round"').replace(/url\(#(\w+)\)/g, 'url(#$1_round)')}</div><div class="caption t3" style="margin-top:10px">Adaptive (circle mask)</div></div>
  <div style="text-align:center"><div style="width:180px;height:180px;border-radius:40px;background:#F3F1F8;display:flex;align-items:center;justify-content:center">${mark(140)}</div><div class="caption t3" style="margin-top:10px">Mark on light</div></div>
  <div style="text-align:center"><div style="display:flex;gap:18px;align-items:flex-end">${[64, 40, 24].map(s => mark(s)).join('')}</div><div class="caption t3" style="margin-top:10px">Small sizes</div></div>
 </div>
 <div class="glass r-xl" style="margin-top:36px;padding:28px 32px;display:flex;align-items:center;justify-content:space-between">${wordmark(56)}<div class="body t2" style="max-width:360px">Wordmark: <b style="color:var(--t1)">Photo</b> in heavy weight, <span style="color:var(--accent)">Plus</span> in light ember. Confident, editorial, unmistakably photographic.</div></div>
 <div class="title3" style="margin:36px 0 12px">Darkroom Aurora palette</div>
 <div class="row" style="gap:12px">${sw('Ink', '#05050A')}${sw('Ember', '#FF7A45', '#1a0d06')}${sw('Rose', '#F2477E')}${sw('Orchid', '#C04DFF')}${sw('Indigo', '#5B5BF7')}${sw('Lagoon', '#2DD4BF', '#04201c')}${sw('Gold', '#F5C66B', '#1A1206')}</div>
 <div class="row" style="gap:12px;margin-top:12px">${sw('Spectrum (decorative)', G.spectrum)}${sw('Action (AA text)', G.action)}${sw('Premium', G.premium, '#1A1206')}${sw('Lagoon', G.lagoon)}</div>
 <div class="row" style="gap:16px;margin-top:28px;align-items:center"><div class="btn primary">${icon('camera', 20, '#fff', 2)}Primary</div><div class="btn glassb">${icon('wand', 20)}Glass</div><div class="btn premium">${icon('crown', 20, '#1A1206', 2)}Premium</div><div class="btn ghost">Ghost</div>${premiumBadge}<span class="badge b-ok">${icon('check', 12, '#3DDC97', 2.2)}Saved</span><div class="iconbtn" style="width:48px;height:48px">${icon('flip', 21)}</div></div>
 </div></div>`;

// ------------------------------------------------------------------ render --
const page = (inner, w, h) => `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body style="width:${w}px;height:${h}px">${inner}</body></html>`;
const browser = await chromium.launch();
const ctx = await browser.newContext({ deviceScaleFactor: 2 });
const p = await ctx.newPage();

const order = [['onboarding', 'Welcome'], ['studio', 'Studio'], ['camera', 'Camera'], ['projects', 'Projects'], ['packs', 'Packs'], ['settings', 'Settings'], ['device', 'Device']];
for (const [key] of order) {
  await p.setViewportSize({ width: 390, height: 844 });
  await p.setContent(page(`<div class="phone" style="border-radius:0">${screens[key]}</div>`, 390, 844));
  await p.screenshot({ path: join(out, `screen-${key}.png`) });
}

// Overview: all screens side by side in device frames.
const frames = order.map(([key, label]) => `<div style="display:flex;flex-direction:column;align-items:center;gap:14px"><div style="padding:10px;border-radius:54px;background:linear-gradient(160deg,#2a2638,#0d0b14);box-shadow:0 30px 60px -20px rgba(0,0,0,.7), inset 0 0 0 1px rgba(255,255,255,.08)"><div class="phone">${screens[key]}</div></div><div class="overline t2">${label}</div></div>`);
const overview = (items, title) => `<div style="padding:48px 56px 56px;background:radial-gradient(ellipse at 20% 0%, #24172f 0%, #0b0a10 55%)"><div class="row" style="justify-content:space-between;margin-bottom:34px">${wordmark(40)}<div class="callout t2">${title}</div></div><div style="display:flex;gap:36px">${items.join('')}</div></div>`;
await p.setViewportSize({ width: 4 * 410 + 3 * 36 + 112, height: 1050 });
await p.setContent(page(overview(frames.slice(0, 4), 'Phase 1 · Design preview (1/2) · Dark theme · "Darkroom Aurora"'), 4 * 410 + 3 * 36 + 112, 1050));
await p.screenshot({ path: join(out, 'overview-1.png'), fullPage: true });
await p.setViewportSize({ width: 3 * 410 + 2 * 36 + 112, height: 1050 });
await p.setContent(page(overview(frames.slice(4), 'Phase 1 · Design preview (2/2)'), 3 * 410 + 2 * 36 + 112, 1050));
await p.screenshot({ path: join(out, 'overview-2.png'), fullPage: true });

await p.setViewportSize({ width: 1200, height: 1000 });
await p.setContent(page(brand, 1200, 1000));
await p.screenshot({ path: join(out, 'brand-sheet.png'), fullPage: true });

await browser.close();
console.log('Design preview rendered to docs/design');
