#!/usr/bin/env node
/**
 * Renders the Spotlight / Glow / Vibe Match design preview
 * (docs/design/spotlight-*.png) from real engine output:
 *
 *   python3 scripts/make-spotlight-assets.py KODAK_DIR portrait_demo MASK_DIR /tmp/pp-spot
 *   python3 scripts/make-transform-assets.py KODAK_DIR transform_demo enhance_bench /tmp/pp-spot
 *   node scripts/render-spotlight-preview.mjs /tmp/pp-spot
 * (Transform screens are rendered when their assets are present.)
 *
 * Layout, sizes and colours mirror src/screens/camera/* (chip 36 dp, shutter
 * 82 dp, preview frame sized to the aspect ratio below a 56 dp top bar) and
 * icons are parsed from src/components/Icon.tsx. The photos are real: the
 * viewfinder is a Kodak test image, and the before/after screen shows a
 * simulated weak-camera burst and the engine's actual output for it.
 */
import { existsSync, readFileSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const assets = process.argv[2];
if (!assets) {
  console.error('usage: node scripts/render-spotlight-preview.mjs ASSET_DIR');
  process.exit(1);
}
const out = join(root, 'docs/design');
mkdirSync(out, { recursive: true });

const uri = file => `data:image/png;base64,${readFileSync(join(assets, file)).toString('base64')}`;
const K18 = uri('kodim18.png');
const SPOT18 = uri('spot18.png');
const FACE18 = uri('face18.png');
const GLOWFACE18 = uri('glowface18.png');
const GLOW = JSON.parse(readFileSync(join(assets, 'glow18.json'), 'utf8'));
const DEPTH18 = uri('depth18.png');
const FEET18 = uri('feet18.png');
const SPOTFEET18 = uri('spotfeet18.png');
const REF = uri('look_ref.png');
const VIBE18 = uri('vibe18.png');
const EMBLEM = `data:image/png;base64,${readFileSync(join(root, 'src/assets/brand/logo-emblem@3x.png')).toString('base64')}`;

// ---- icons parsed from Icon.tsx (same approach as render-design-preview) ----
const iconSrc = readFileSync(join(root, 'src/components/Icon.tsx'), 'utf8');
const body = iconSrc.slice(iconSrc.indexOf('export const ICONS = {'), iconSrc.indexOf('} as const;'));
const ICONS = {};
for (const m of body.matchAll(/\n {2}(\w+): ([\s\S]*?)(?=,\n {2}\w+: |,\n$)/g)) {
  ICONS[m[1]] = m[2]
    .replace(/<\/?>/g, '')
    .replace(/<Path/g, '<path')
    .replace(/<Circle/g, '<circle')
    .replace(/=\{([^}]+)\}/g, '="$1"')
    .replace(/^\s*\(\s*|\s*\)\s*$/g, '');
}
const icon = (name, size = 18, color = '#FFFFFF', sw = 2) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round">${ICONS[name]}</svg>`;

const GOLD = '#F5C66B';
const SPECTRUM = 'linear-gradient(135deg, #FF7A45 0%, #F2477E 48%, #8B5CF6 100%)';
const ACTION = 'linear-gradient(135deg, #CC4128 0%, #C2306A 50%, #7445E0 100%)';
const SHEEN =
  'linear-gradient(160deg, rgba(255,255,255,0.20) 0%, rgba(255,255,255,0.04) 38%, rgba(255,255,255,0.0) 60%, rgba(255,255,255,0.05) 100%)';

const css = `
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Roboto,'Segoe UI',system-ui,sans-serif;background:#0d0c12;color:#F7F5FB;-webkit-font-smoothing:antialiased}
.phone{position:relative;width:390px;height:844px;overflow:hidden;background:#000;border-radius:44px}
.status{position:absolute;top:0;left:0;right:0;height:44px;display:flex;justify-content:space-between;align-items:center;padding:0 26px;font-size:13px;font-weight:600;z-index:5;color:#fff}
.chip{height:36px;min-width:40px;padding:0 10px;border-radius:18px;border:1px solid rgba(255,255,255,.16);background:rgba(16,14,25,.55);display:flex;align-items:center;justify-content:center;gap:5px;font-size:12px;font-weight:700;color:rgba(255,255,255,.86)}
.chip.on{background:rgba(255,255,255,.2);color:#fff}
.row{display:flex;align-items:center}
.caption{font-size:12px;line-height:16px;font-weight:500;letter-spacing:.2px}
.overline{font-size:11px;line-height:14px;font-weight:700;letter-spacing:1.4px;text-transform:uppercase}
.pill{display:inline-flex;align-items:center;gap:8px;height:34px;padding:0 14px;border-radius:17px;background:rgba(10,9,17,.72);border:1px solid rgba(255,255,255,.16);font-size:12px;font-weight:700}
.mono{font-family:monospace;font-size:12px;font-weight:500;letter-spacing:.3px}
.glass{position:relative;background-color:rgba(255,255,255,0.065);border:1px solid rgba(255,255,255,0.14);background-image:${SHEEN};box-shadow:0 18px 40px -12px rgba(0,0,0,0.45);overflow:hidden}
.badge{display:inline-flex;align-items:center;gap:4px;padding:4px 9px;border-radius:999px;font-size:12px;font-weight:700;border:1px solid}
.spin{width:16px;height:16px;border-radius:50%;border:2px solid rgba(245,198,107,.25);border-top-color:currentColor}
`;

const status = `<div class="status"><span>9:41</span><span style="display:flex;gap:6px;align-items:center"><svg width="16" height="11" viewBox="0 0 16 11" fill="currentColor"><rect x="0" y="7" width="3" height="4" rx="1"/><rect x="4.3" y="5" width="3" height="6" rx="1"/><rect x="8.6" y="2.5" width="3" height="8.5" rx="1"/><rect x="13" y="0" width="3" height="11" rx="1"/></svg><svg width="24" height="12" viewBox="0 0 24 12" fill="none" stroke="currentColor"><rect x=".5" y=".5" width="20" height="11" rx="3"/><rect x="2" y="2" width="14" height="8" rx="1.5" fill="currentColor"/><path d="M22.5 4v4"/></svg></span></div>`;

// ---- camera building blocks (mirroring CameraControls.tsx) ----
const FRAME_TOP = 100; // insets.top (44) + 56 dp top bar
const FRAME_H = 520; // 390 / (3/4)
const grid = `<div style="position:absolute;inset:0">${[1, 2]
  .map(
    i =>
      `<div style="position:absolute;left:${i * 33.333}%;top:0;bottom:0;width:1px;background:rgba(255,255,255,.35)"></div><div style="position:absolute;top:${i * 33.333}%;left:0;right:0;height:1px;background:rgba(255,255,255,.35)"></div>`,
  )
  .join('')}</div>`;
const viewfinder = (src, extra = '', pos = 'center') =>
  `<div style="position:absolute;top:${FRAME_TOP}px;left:0;width:390px;height:${FRAME_H}px;overflow:hidden"><img src="${src}" style="width:100%;height:100%;object-fit:cover;object-position:${pos};display:block">${grid}${extra}</div>`;

const topBar = ({ flash = 'off', timer = 0, ev = null, vibe = false }) => `
  <div class="row" style="position:absolute;top:52px;left:12px;right:12px;justify-content:space-between">
    <div class="chip">${icon('close')}</div>
    <div class="row" style="gap:6px">
      <div class="chip ${flash !== 'off' ? 'on' : ''}">${icon(flash === 'off' ? 'flashOff' : 'flash', 18, flash === 'on' ? GOLD : '#fff')}${flash === 'auto' ? 'A' : ''}</div>
      <div class="chip ${timer ? 'on' : ''}">${icon('timer')}${timer ? `${timer}s` : ''}</div>
      <div class="chip ${vibe ? 'on' : ''}">${icon('wand', 18, vibe ? GOLD : '#fff')}</div>
      <div class="chip on">${icon('grid')}</div>
      <div class="chip ${ev ? 'on' : ''}">${icon('exposure')}${ev ?? ''}</div>
      <div class="chip">4:3</div>
    </div>
  </div>`;

const zoom = (active = 1) => `
  <div style="display:flex;justify-content:center"><div class="row" style="gap:8px;padding:4px 6px;border-radius:26px;background:rgba(10,9,17,.45)">${[
    0.6, 1, 2, 5,
  ]
    .map(z =>
      z === active
        ? `<div style="width:44px;height:44px;border-radius:22px;border:1px solid ${GOLD};background:rgba(255,255,255,.12);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800;color:${GOLD}">${z}×</div>`
        : `<div style="width:36px;height:36px;border-radius:22px;background:rgba(255,255,255,.12);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800">${z}</div>`,
    )
    .join('')}</div></div>`;

const MODES = [
  ['night', 'Moonlight', true],
  ['spotlight', 'Spotlight', false],
  ['enhance', 'Clarity', false],
  ['photo', 'Photo', false],
  ['superres', 'Ultra Detail', true],
  ['pro', 'Pro', true],
];
const modes = active => `
  <div style="overflow:hidden"><div class="row" style="justify-content:center;gap:2px;white-space:nowrap;transform:translateX(${{ night: 150, spotlight: 70, enhance: 0, photo: -60 }[active] ?? 0}px)">${MODES.map(
    ([id, label, premium]) =>
      `<div class="row overline" style="gap:4px;padding:8px 10px;border-radius:16px;flex:none;${id === active ? `background:rgba(255,255,255,.14);color:${GOLD}` : 'color:rgba(255,255,255,.78)'}">${premium ? icon('crown', 11, GOLD, 2.2) : ''}${label}</div>`,
  ).join('')}</div></div>`;

const shutter = (mode, busy = false) => {
  const gradient = mode !== 'photo' && mode !== 'pro';
  const centre = { night: 'moon', superres: 'layers', enhance: 'sparkles', spotlight: 'target' }[mode];
  return `<div style="width:82px;height:82px;border-radius:41px;border:4px solid ${mode === 'pro' ? GOLD : '#fff'};display:flex;align-items:center;justify-content:center"><div style="width:64px;height:64px;border-radius:32px;background:#fff;${gradient ? `background-image:${SPECTRUM};` : ''}display:flex;align-items:center;justify-content:center;transform:scale(${busy ? 0.84 : 1})">${centre ? icon(centre, 24) : ''}</div></div>`;
};
const dock = (mode, { thumb = K18, busy = false } = {}) => `
  <div class="row" style="justify-content:space-between;padding:0 32px">
    <div style="width:50px;height:50px;border-radius:14px;overflow:hidden;border:1.5px solid rgba(255,255,255,.6);position:relative"><img src="${thumb}" style="width:100%;height:100%;object-fit:cover">${busy ? `<div style="position:absolute;inset:0;background:rgba(5,5,10,.55);display:flex;align-items:center;justify-content:center;color:${GOLD}"><div class="spin" style="width:22px;height:22px"></div></div>` : ''}</div>
    ${shutter(mode, false)}
    <div class="chip" style="width:40px">${icon('flip')}</div>
  </div>`;
const bottom = inner =>
  `<div style="position:absolute;left:0;right:0;bottom:46px;display:flex;flex-direction:column;gap:10px">${inner}</div>`;
const captionLine = text =>
  `<div class="caption" style="color:rgba(255,255,255,.7);text-align:center;padding:0 24px">${text}</div>`;

// ---- screens ----
// Every number shown comes from the engine run that made the assets (e.g.
// the blemish count is portrait_demo's report for kodim18 at Flawless).
const pill = (label, on) =>
  `<div style="height:30px;padding:0 12px;border-radius:15px;border:1px solid ${on ? GOLD : 'rgba(255,255,255,.22)'};background:${on ? 'rgba(245,198,107,.16)' : 'transparent'};display:flex;align-items:center;font-size:12px;font-weight:700;color:${on ? GOLD : '#fff'}">${label}</div>`;
const spotPanel = `
  <div style="margin:0 12px;border-radius:22px;padding:12px;display:flex;flex-direction:column;gap:9px;background:rgba(10,9,17,.66);border:1px solid rgba(255,255,255,.14);background-image:linear-gradient(160deg, rgba(255,255,255,0.12) 0%, rgba(255,255,255,0.02) 40%, rgba(255,255,255,0) 100%)">
    <div class="row caption" style="gap:8px;color:rgba(255,255,255,.86)">${icon('target', 14)}<span style="flex:1">1 chosen · tap again to remove</span><b style="color:#fff">Clear</b></div>
    <div class="row" style="gap:6px"><span class="caption" style="width:38px;color:rgba(255,255,255,.7);font-weight:700">Blur</span>${pill('Light')}${pill('Medium', true)}${pill('Strong')}</div>
    <div class="row" style="gap:6px"><span class="caption" style="width:38px;color:rgba(255,255,255,.7);font-weight:700">Glow</span>${pill('Off')}${pill('Natural')}${pill('Smooth', true)}${pill('Flawless')}</div>
  </div>`;
const marker = (x, y) =>
  `<div style="position:absolute;left:${x - 17}px;top:${y - 17}px;width:34px;height:34px;border-radius:17px;border:2px solid ${GOLD};background:rgba(10,9,17,.25);display:flex;align-items:center;justify-content:center"><div style="width:18px;height:18px;border-radius:9px;background:${GOLD};color:#1A1206;font-size:11px;font-weight:800;display:flex;align-items:center;justify-content:center">1</div></div>`;

const cameraSpotlight = `${viewfinder(K18, marker(235, 230), '60% 30%')}${status}${topBar({ flash: 'off', vibe: false })}
  ${bottom(`${spotPanel}${zoom(1)}${modes('spotlight')}${dock('spotlight')}${captionLine('Your subject shines, the background melts away')}`)}`;

const aurora = `<div style="position:absolute;inset:0;background:#05050A">
  <div style="position:absolute;top:-170px;left:-150px;width:460px;height:460px;border-radius:50%;filter:blur(40px);opacity:.45;background-image:radial-gradient(circle,#FF7A45 0%,#FF7A45 18%,transparent 70%)"></div>
  <div style="position:absolute;bottom:-220px;left:10%;width:480px;height:480px;border-radius:50%;filter:blur(40px);opacity:.4;background-image:radial-gradient(circle,#5B5BF7 0%,#5B5BF7 18%,transparent 70%)"></div></div>`;
const header = (overline, title) =>
  `<div class="row" style="gap:12px;margin-bottom:14px"><div style="width:44px;height:44px;border-radius:22px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.065);border:1px solid rgba(255,255,255,.14)">${icon('chevronLeft', 19, '#fff', 1.75)}</div><div style="flex:1"><div class="overline" style="color:#FF7A45">${overline}</div><div style="font-size:22px;line-height:28px;font-weight:700">${title}</div></div><div style="width:44px;height:44px;border-radius:22px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.065);border:1px solid rgba(255,255,255,.14)">${icon('share', 19, '#fff', 1.75)}</div></div>`;
const compareBox = (before, after, w, h, split = 0.5) => `
  <div style="position:relative;width:${w}px;height:${h}px;margin:0 auto;border-radius:18px;overflow:hidden;background:#000">
    <img src="${after}" style="position:absolute;inset:0;width:100%;height:100%;object-fit:cover">
    <div style="position:absolute;left:0;top:0;bottom:0;width:${w * split}px;overflow:hidden"><img src="${before}" style="width:${w}px;height:${h}px;object-fit:cover"></div>
    <div style="position:absolute;left:${w * split - 1}px;top:0;bottom:0;width:2px;background:#fff;display:flex;align-items:center;justify-content:center"><div style="width:40px;height:40px;flex:none;border-radius:20px;border:2px solid ${GOLD};background:rgba(10,9,17,.75);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800">‹ ›</div></div>
    <div class="overline" style="position:absolute;left:10px;top:10px;padding:3px 8px;border-radius:10px;background:rgba(10,9,17,.6)">Before</div>
    <div class="overline" style="position:absolute;right:10px;top:10px;padding:3px 8px;border-radius:10px;background:rgba(10,9,17,.6);color:${GOLD}">After</div>
  </div>`;
const badge = (label, ic, tone) => {
  const c = {
    ok: ['rgba(61,220,151,.14)', '#3DDC97', 'rgba(61,220,151,.4)'],
    acc: ['rgba(255,122,69,.14)', '#FF7A45', 'rgba(255,122,69,.4)'],
    gold: ['rgba(245,198,107,.16)', GOLD, 'rgba(245,198,107,.45)'],
  }[tone];
  return `<span class="badge" style="background:${c[0]};color:${c[1]};border-color:${c[2]}">${icon(ic, 12, c[1], 2.2)}${label}</span>`;
};
const improved = items =>
  `<div class="glass" style="border-radius:24px;padding:14px 16px;margin-top:14px"><div style="font-size:18px;line-height:24px;font-weight:700;margin-bottom:8px">What we improved</div><div class="row" style="gap:8px;flex-wrap:wrap">${items.join('')}</div></div>`;
const actions = `<div class="row" style="gap:10px;margin-top:14px"><div style="flex:1;height:52px;border-radius:26px;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;background-image:${ACTION}">${icon('check', 18, '#fff')}Keep</div><div style="flex:1;height:52px;border-radius:26px;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;background:rgba(255,255,255,.11);border:1px solid rgba(255,255,255,.14)">${icon('share', 18, '#fff')}Share</div></div>`;

const spotResult = `${status}${aurora}<div style="position:absolute;inset:0;padding:58px 16px 0">${header('Spotlight', 'Your photo')}
  ${compareBox(K18, SPOT18, 358, 537, 0.42)}
  ${improved([badge('Background softened', 'target', 'acc')])}${actions}</div>`;

// Her face is ~55 px across in the Kodak photo: shown enlarged (same crop of both).
const glowResult = `${status}${aurora}<div style="position:absolute;inset:0;padding:58px 16px 0">${header('Spotlight · Glow Flawless', 'Zoomed in')}
  ${compareBox(FACE18, GLOWFACE18, 358, 500, 0.5)}
  ${improved([badge(`Glow · ${GLOW.spots} blemishes cleared`, 'sparkles', 'ok'), badge('Background softened', 'target', 'acc')])}${actions}</div>`;

const vibeResult = `${status}${aurora}<div style="position:absolute;inset:0;padding:58px 16px 0">${header('Vibe Match', 'Your photo')}
  <div class="glass" style="border-radius:20px;padding:10px 12px;margin-bottom:12px;display:flex;align-items:center;gap:12px"><img src="${REF}" style="width:58px;height:58px;border-radius:12px;object-fit:cover"><div style="flex:1"><div class="row" style="gap:6px;font-weight:700">${icon('wand', 16, GOLD)}Style from your photo</div><div class="caption" style="color:rgba(247,245,251,.7)">Your camera now shoots in this style.</div></div>${icon('crown', 16, GOLD, 2.2)}</div>
  ${compareBox(K18, VIBE18, 358, 470, 0.5)}
  ${improved([badge('Vibe Match style', 'wand', 'gold')])}</div>`;

// ---- render ----
const page = (inner, w, h) =>
  `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body style="width:${w}px;height:${h}px">${inner}</body></html>`;
const browser = await chromium.launch();
const ctx = await browser.newContext({ deviceScaleFactor: 2 });
const p = await ctx.newPage();
const screens = [
  ['camera', 'Spotlight · tap what should shine', cameraSpotlight],
  ['result', 'Spotlight · before / after', spotResult],
  ['glow', 'Glow skin · Flawless', glowResult],
  ['vibe', 'Vibe Match · your style photo', vibeResult],
];
for (const [key, , html] of screens) {
  await p.setViewportSize({ width: 390, height: 844 });
  await p.setContent(page(`<div class="phone" style="border-radius:0">${html}</div>`, 390, 844));
  await p.screenshot({ path: join(out, `spotlight-${key}.png`) });
}
const W = 4 * 410 + 3 * 36 + 112;
const frames = screens
  .map(
    ([, label, html]) =>
      `<div style="display:flex;flex-direction:column;align-items:center;gap:14px"><div style="padding:10px;border-radius:54px;background:linear-gradient(160deg,#2a2638,#0d0b14);box-shadow:0 30px 60px -20px rgba(0,0,0,.7), inset 0 0 0 1px rgba(255,255,255,.08)"><div class="phone">${html}</div></div><div class="overline" style="color:rgba(247,245,251,.74)">${label}</div></div>`,
  )
  .join('');
const overview = `<div style="padding:48px 56px 56px;background:radial-gradient(ellipse at 20% 0%, #24172f 0%, #0b0a10 55%)">
  <div class="row" style="justify-content:space-between;margin-bottom:34px"><div class="row" style="gap:11px"><img src="${EMBLEM}" width="40" height="40" style="border-radius:9px"><span style="font-size:26px;font-weight:900;letter-spacing:-.6px">Photo<span style="color:#FFB01F">Plus</span></span></div>
  <div style="font-size:13px;color:rgba(247,245,251,.74)">Spotlight · Glow · Vibe Match · real results on Kodak test photos</div></div>
  <div style="display:flex;gap:36px">${frames}</div></div>`;
await p.setViewportSize({ width: W, height: 1050 });
await p.setContent(page(overview, W, 1050));
await p.screenshot({ path: join(out, 'spotlight-overview.png'), fullPage: true });

// How Spotlight sees depth: photo, depth (near = bright), result, and the
// ground at her feet before / after (stays sharp; ground further back softens).
const panel = (src, label, w = 300, h = 450) =>
  `<div style="display:flex;flex-direction:column;gap:10px"><img src="${src}" style="width:${w}px;height:${h}px;object-fit:cover;border-radius:18px"><div class="overline" style="color:rgba(247,245,251,.74)">${label}</div></div>`;
const depthPage = `<div style="padding:40px 48px;background:radial-gradient(ellipse at 20% 0%, #24172f 0%, #0b0a10 55%)">
  <div style="font-size:24px;font-weight:800;margin-bottom:6px">Spotlight blurs by distance, like a real lens</div>
  <div class="caption" style="color:rgba(247,245,251,.74);margin-bottom:24px">The ground she stands on stays sharp; everything behind her softens the further back it is.</div>
  <div style="display:flex;gap:24px;align-items:flex-start">
    ${panel(K18, 'Your photo')}${panel(DEPTH18, 'Distance · near is bright')}${panel(SPOT18, 'Spotlight')}
    <div style="display:flex;flex-direction:column;gap:14px">${panel(FEET18, 'At her feet · before', 362, 210)}${panel(SPOTFEET18, 'At her feet · Spotlight', 362, 210)}</div>
  </div></div>`;
await p.setViewportSize({ width: 1430, height: 640 });
await p.setContent(page(depthPage, 1430, 640));
await p.screenshot({ path: join(out, 'spotlight-depth.png'), fullPage: true });

// ---- Transform (assets from scripts/make-transform-assets.py) ----
if (existsSync(join(assets, 't_report.json'))) {
  const T = JSON.parse(readFileSync(join(assets, 't_report.json'), 'utf8'));
  const BASIC = uri('t_basic.png');
  const FACE_B = uri('t_face_basic.png');
  const FACE_T = uri('t_face_tf.png');
  const signs = [
    T.lens.detail >= 1.1 ? 'soft fine detail' : null,
    Math.sqrt(Math.max(T.lens.corner ** 2 - T.lens.detail ** 2, 0)) >= 0.8 ? 'blurry edges' : null,
    T.lens.fringe >= 1 ? 'colour fringes on edges' : null,
    T.lens.noise >= 0.014 ? 'grain' : null,
    'low resolution',
  ].filter(Boolean);
  const grain = Math.round((1 - T.transform.noise[1] / T.transform.noise[0]) * 100);
  const button = (label, ic, premium) =>
    `<div style="height:48px;border-radius:24px;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;${premium ? `background:linear-gradient(135deg,#F5C66B,#E8A23A);color:#1A1206` : `background-image:${ACTION}`}">${icon(ic, 18, premium ? '#1A1206' : '#fff')}${label}</div>`;
  const notice = `${status}${aurora}<div style="position:absolute;inset:0;padding:58px 16px 0">${header('Photo', 'Clarity')}
    <div style="width:358px;height:300px;margin:0 auto 14px;border-radius:18px;overflow:hidden"><img src="${BASIC}" style="width:100%;height:100%;object-fit:cover;object-position:50% 40%"></div>
    <div class="glass" style="border-radius:24px;padding:16px;display:flex;flex-direction:column;gap:10px">
      <div class="row" style="gap:8px">${icon('aperture', 20, GOLD)}<div style="font-size:18px;line-height:24px;font-weight:700;flex:1">This photo could look sharper</div></div>
      <div style="font-size:15px;line-height:21px;color:rgba(247,245,251,.74)">It shows signs of a basic camera lens, so it may not reach top quality as it is. Transform gives it the look of a high-end camera, and keeps your effects.</div>
      <div class="caption" style="color:rgba(247,245,251,.6)">Seen in this photo: ${signs.join(', ')}.</div>
      ${button('Try Transform', 'aperture', true)}
      <div class="caption" style="text-align:center;color:rgba(247,245,251,.6)">2 free Transforms left today</div>
    </div></div>`;
  const tfBadges = [
    badge('High-end lens look', 'aperture', 'gold'),
    T.transform.fringeFixed >= 0.5 ? badge('Colour fringes removed', 'check', 'ok') : '',
    badge('Sharper detail', 'sparkles', 'ok'),
    badge('2× detail', 'crown', 'gold'),
    grain >= 5 ? badge(`Grain −${grain}%`, 'sparkles', 'ok') : '',
  ];
  const result = `${status}${aurora}<div style="position:absolute;inset:0;padding:58px 16px 0">${header('Transform', 'Zoomed in')}
    ${compareBox(FACE_B, FACE_T, 358, 358, 0.5)}
    ${improved(tfBadges)}${actions}</div>`;
  for (const [key, html] of [
    ['notice', notice],
    ['result', result],
  ]) {
    await p.setViewportSize({ width: 390, height: 844 });
    await p.setContent(page(`<div class="phone" style="border-radius:0">${html}</div>`, 390, 844));
    await p.screenshot({ path: join(out, `transform-${key}.png`) });
  }
  // Detail sheet: the same regions from the basic lens, Clarity alone and Transform.
  const cell = (src, label) =>
    `<div style="display:flex;flex-direction:column;gap:8px"><img src="${src}" style="width:300px;height:300px;border-radius:14px;image-rendering:auto"><div class="overline" style="color:rgba(247,245,251,.74)">${label}</div></div>`;
  const rows = ['face', 'hat']
    .map(
      r =>
        `<div style="display:flex;gap:18px">${cell(uri(`t_${r}_basic.png`), 'Basic lens')}${cell(uri(`t_${r}_clarity.png`), 'Clarity')}${cell(uri(`t_${r}_tf.png`), 'Transform · 2× detail')}</div>`,
    )
    .join('');
  const sheet = `<div style="padding:36px 44px;background:radial-gradient(ellipse at 20% 0%, #24172f 0%, #0b0a10 55%)">
    <div style="font-size:24px;font-weight:800;margin-bottom:6px">Transform: the look of a high-end camera</div>
    <div class="caption" style="color:rgba(247,245,251,.74);margin-bottom:22px">A photo taken through a simulated basic phone lens, zoomed in. Transform restores fine detail and sharp edges, removes colour fringes and grain, and keeps the photo's own colours.</div>
    <div style="display:flex;flex-direction:column;gap:20px">${rows}</div></div>`;
  await p.setViewportSize({ width: 1040, height: 900 });
  await p.setContent(page(sheet, 1040, 900));
  await p.screenshot({ path: join(out, 'transform-detail.png'), fullPage: true });
}

await browser.close();
console.log('Spotlight preview rendered to docs/design');
