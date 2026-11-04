#!/usr/bin/env node
/**
 * Renders the Phase 2 camera design preview (docs/design/phase2-*.png).
 *
 *   python3 scripts/make-preview-assets.py kodim19.png enhance_bench /tmp/pp-assets
 *   node scripts/render-phase2-preview.mjs /tmp/pp-assets
 *
 * Layout, sizes and colours mirror src/screens/camera/* (chip 36 dp, shutter
 * 82 dp, preview frame sized to the aspect ratio below a 56 dp top bar) and
 * icons are parsed from src/components/Icon.tsx. The photos are real: the
 * viewfinder is a Kodak test image, and the before/after screen shows a
 * simulated weak-camera burst and the engine's actual output for it.
 */
import { readFileSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const assets = process.argv[2];
if (!assets) {
  console.error('usage: node scripts/render-phase2-preview.mjs ASSET_DIR');
  process.exit(1);
}
const out = join(root, 'docs/design');
mkdirSync(out, { recursive: true });

const uri = (file, mime) => `data:${mime};base64,${readFileSync(join(assets, file)).toString('base64')}`;
const SCENE = uri('scene.jpg', 'image/jpeg');
const NIGHT = uri('night.jpg', 'image/jpeg');
const PEAKING = uri('peaking.png', 'image/png');
const WEAK = uri('weak-upscaled.png', 'image/png');
const ENHANCED = uri('enhanced.png', 'image/png');
const report = JSON.parse(readFileSync(join(assets, 'report.json'), 'utf8'));
const noiseCut = Math.round((1 - report.noiseAfter / report.noiseBefore) * 100);
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
const viewfinder = (src, extra = '') =>
  `<div style="position:absolute;top:${FRAME_TOP}px;left:0;width:390px;height:${FRAME_H}px;overflow:hidden"><img src="${src}" style="width:100%;height:100%;object-fit:cover;display:block">${grid}${extra}</div>`;

const topBar = ({ flash = 'off', timer = 0, ev = null }) => `
  <div class="row" style="position:absolute;top:52px;left:12px;right:12px;justify-content:space-between">
    <div class="chip">${icon('close')}</div>
    <div class="row" style="gap:6px">
      <div class="chip ${flash !== 'off' ? 'on' : ''}">${icon(flash === 'off' ? 'flashOff' : 'flash', 18, flash === 'on' ? GOLD : '#fff')}${flash === 'auto' ? 'A' : ''}</div>
      <div class="chip ${timer ? 'on' : ''}">${icon('timer')}${timer ? `${timer}s` : ''}</div>
      <div class="chip on">${icon('grid')}</div>
      <div class="chip ${ev ? 'on' : ''}">${icon('exposure')}${ev ?? ''}</div>
      <div class="chip">4:3</div>
      <div class="chip">12MP</div>
    </div>
  </div>`;

const zoom = (active = 1) => `
  <div style="display:flex;justify-content:center"><div class="row" style="gap:8px;padding:4px 6px;border-radius:26px;background:rgba(10,9,17,.45)">${[0.6, 1, 2, 5]
    .map(z =>
      z === active
        ? `<div style="width:44px;height:44px;border-radius:22px;border:1px solid ${GOLD};background:rgba(255,255,255,.12);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800;color:${GOLD}">${z}×</div>`
        : `<div style="width:36px;height:36px;border-radius:22px;background:rgba(255,255,255,.12);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800">${z}</div>`,
    )
    .join('')}</div></div>`;

const MODES = [
  ['night', 'Night', true],
  ['enhance', 'Enhance', false],
  ['photo', 'Photo', false],
  ['superres', 'Super-res', true],
  ['pro', 'Pro', true],
];
const modes = active => `
  <div class="row" style="justify-content:center;gap:2px;white-space:nowrap">${MODES.map(
    ([id, label, premium]) =>
      `<div class="row overline" style="gap:4px;padding:8px 10px;border-radius:16px;flex:none;${id === active ? `background:rgba(255,255,255,.14);color:${GOLD}` : 'color:rgba(255,255,255,.78)'}">${premium ? icon('crown', 11, GOLD, 2.2) : ''}${label}</div>`,
  ).join('')}</div>`;

const shutter = (mode, busy = false) => {
  const gradient = mode !== 'photo' && mode !== 'pro';
  const centre = { night: 'moon', superres: 'layers', enhance: 'sparkles' }[mode];
  return `<div style="width:82px;height:82px;border-radius:41px;border:4px solid ${mode === 'pro' ? GOLD : '#fff'};display:flex;align-items:center;justify-content:center"><div style="width:64px;height:64px;border-radius:32px;background:#fff;${gradient ? `background-image:${SPECTRUM};` : ''}display:flex;align-items:center;justify-content:center;transform:scale(${busy ? 0.84 : 1})">${centre ? icon(centre, 24) : ''}</div></div>`;
};
const dock = (mode, { thumb = SCENE, busy = false } = {}) => `
  <div class="row" style="justify-content:space-between;padding:0 32px">
    <div style="width:50px;height:50px;border-radius:14px;overflow:hidden;border:1.5px solid rgba(255,255,255,.6);position:relative"><img src="${thumb}" style="width:100%;height:100%;object-fit:cover">${busy ? `<div style="position:absolute;inset:0;background:rgba(5,5,10,.55);display:flex;align-items:center;justify-content:center;color:${GOLD}"><div class="spin" style="width:22px;height:22px"></div></div>` : ''}</div>
    ${shutter(mode, false)}
    <div class="chip" style="width:40px">${icon('flip')}</div>
  </div>`;
const bottom = inner => `<div style="position:absolute;left:0;right:0;bottom:46px;display:flex;flex-direction:column;gap:10px">${inner}</div>`;
const captionLine = text =>
  `<div class="caption" style="color:rgba(255,255,255,.7);text-align:center;padding:0 24px">${text}</div>`;

const focusRing = (x, y, locked = true) =>
  `<div style="position:absolute;left:${x - 38}px;top:${y - 38}px;width:76px;height:76px;border-radius:38px;border:1.5px solid ${locked ? GOLD : '#fff'};display:flex;align-items:center;justify-content:center"><div style="width:4px;height:4px;border-radius:2px;background:${locked ? GOLD : '#fff'}"></div></div>`;

// ---- screens ----
const photoMode = `${viewfinder(SCENE, focusRing(250, 180))}${status}${topBar({ flash: 'auto' })}
  <div style="position:absolute;top:108px;left:0;right:0;display:flex;justify-content:center"><span class="badge" style="background:rgba(61,220,151,.14);color:#3DDC97;border-color:rgba(61,220,151,.4)">${icon('check', 12, '#3DDC97', 2.2)}Saved to Captures</span></div>
  ${bottom(`${zoom(1)}${modes('photo')}${dock('photo')}${captionLine('The camera’s own photo')}`)}`;

const ticks = (values, selected) =>
  values
    .map(
      (v, i) =>
        `<div style="width:58px;flex:none;display:flex;flex-direction:column;align-items:center;gap:4px"><div style="width:1.5px;height:${i % 3 === 0 ? 16 : 10}px;border-radius:1px;background:rgba(255,255,255,${i % 3 === 0 ? 0.8 : 0.45})"></div><div style="font-size:11px;font-weight:500;color:${v === selected ? '#fff' : 'rgba(255,255,255,.55)'}">${v}</div></div>`,
    )
    .join('');
const proPanel = `
  <div style="margin:0 12px;border-radius:24px;background:rgba(10,9,17,.66);border:1px solid rgba(255,255,255,.14);padding:10px 0;background-image:linear-gradient(160deg, rgba(255,255,255,0.12) 0%, rgba(255,255,255,0.02) 40%, rgba(255,255,255,0) 100%)">
    <div class="row" style="gap:6px;padding:0 10px">${[
      ['aperture', 'ISO', false, true],
      ['timer', 'Shutter', true, true],
      ['target', 'Focus', false, false],
      ['thermometer', 'WB', false, true],
      ['sliders', 'Assist', false, false],
    ]
      .map(
        ([ic, label, sel, manual]) =>
          `<div class="row caption" style="gap:5px;padding:0 12px;height:34px;border-radius:17px;font-weight:700;${sel ? `background:rgba(255,255,255,.12);color:${GOLD}` : ''}">${icon(ic, 14, sel ? GOLD : '#fff')}${label}${manual ? `<div style="width:5px;height:5px;border-radius:3px;background:${GOLD}"></div>` : ''}</div>`,
      )
      .join('')}</div>
    <div class="row" style="gap:8px;padding:6px 12px 0">
      <div style="flex:1;overflow:hidden">
        <div style="text-align:center;font-size:15px;font-weight:600;color:${GOLD};margin-bottom:6px">1/250</div>
        <div style="position:relative;height:44px;display:flex;align-items:center;overflow:hidden">
          <div style="display:flex;position:absolute;left:${(312 - 58) / 2 - 4 * 58}px">${ticks(['1/4000', '1/2000', '1/1000', '1/500', '1/250', '1/125', '1/60', '1/30', '1/15', '1/8'], '1/250')}</div>
          <div style="position:absolute;left:155px;top:0;width:2px;height:20px;border-radius:1px;background:${GOLD}"></div>
        </div>
      </div>
      <div style="width:36px;height:36px;border-radius:18px;border:1.5px solid rgba(255,255,255,.6);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800">A</div>
    </div>
  </div>`;
const proMode = `${viewfinder(SCENE, `<img src="${PEAKING}" style="position:absolute;inset:0;width:100%;height:100%;object-fit:cover;opacity:.95">`)}${status}${topBar({ flash: 'off' })}
  <div style="position:absolute;top:108px;left:0;right:0;display:flex;justify-content:center"><div class="row" style="gap:6px;padding:0 12px;height:28px;border-radius:14px;background:rgba(10,9,17,.6)"><span class="mono">ISO 400  ·  1/250  ·  ƒ/1.8  ·  2 m  ·  M</span><div style="width:6px;height:6px;border-radius:3px;background:#3DDC97"></div></div></div>
  ${bottom(`${proPanel}${zoom(1)}${modes('pro')}${dock('pro')}${captionLine('Manual ISO, shutter, focus and white balance')}`)}`;

const nightMode = `${viewfinder(NIGHT)}${status}${topBar({ flash: 'off', timer: 3 })}
  <div style="position:absolute;top:108px;left:0;right:0;display:flex;justify-content:center"><div class="pill" style="color:${GOLD}"><div class="spin"></div>Fusing 6 frames${icon('close', 14, '#fff', 2.2)}</div></div>
  ${bottom(`${zoom(1)}${modes('night')}${dock('night', { thumb: NIGHT, busy: true })}${captionLine('Fuses a burst to clean up low light · 6 frames')}`)}`;

const compare = `${status}<div style="position:absolute;inset:0;background:#05050A">
  <div style="position:absolute;top:-170px;left:-150px;width:460px;height:460px;border-radius:50%;filter:blur(40px);opacity:.45;background-image:radial-gradient(circle,#FF7A45 0%,#FF7A45 18%,transparent 70%)"></div>
  <div style="position:absolute;bottom:-220px;left:10%;width:480px;height:480px;border-radius:50%;filter:blur(40px);opacity:.4;background-image:radial-gradient(circle,#5B5BF7 0%,#5B5BF7 18%,transparent 70%)"></div></div>
  <div style="position:absolute;inset:0;padding:58px 16px 0">
    <div class="row" style="gap:12px;margin-bottom:16px"><div style="width:44px;height:44px;border-radius:22px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.065);border:1px solid rgba(255,255,255,.14)">${icon('chevronLeft', 19, '#fff', 1.75)}</div><div><div class="overline" style="color:#FF7A45">3 Nov 2026, 17:42</div><div style="font-size:22px;line-height:28px;font-weight:700">Super-res</div></div></div>
    <div class="row" style="padding:4px;border-radius:999px;background:rgba(255,255,255,.065);border:1px solid rgba(255,255,255,.14);margin-bottom:14px"><div style="flex:1;height:36px;border-radius:999px;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:700;color:rgba(247,245,251,.74)">Photo</div><div style="flex:1;height:36px;border-radius:999px;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:700;background-image:${ACTION}">Before / after</div></div>
    <div style="position:relative;width:332px;height:498px;margin:0 auto;border-radius:18px;overflow:hidden;background:#000">
      <img src="${ENHANCED}" style="position:absolute;inset:0;width:100%;height:100%">
      <div style="position:absolute;left:0;top:0;bottom:0;width:166px;overflow:hidden"><img src="${WEAK}" style="width:332px;height:498px"></div>
      <div style="position:absolute;left:165px;top:0;bottom:0;width:2px;background:#fff;display:flex;align-items:center;justify-content:center"><div style="width:40px;height:40px;flex:none;border-radius:20px;border:2px solid ${GOLD};background:rgba(10,9,17,.75);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:800">‹ ›</div></div>
      <div class="overline" style="position:absolute;left:10px;top:10px;padding:3px 8px;border-radius:10px;background:rgba(10,9,17,.6)">Before</div>
      <div class="overline" style="position:absolute;right:10px;top:10px;padding:3px 8px;border-radius:10px;background:rgba(10,9,17,.6);color:${GOLD}">After</div>
    </div>
    <div class="glass" style="border-radius:24px;padding:14px 16px;margin-top:14px">
      <div style="font-size:18px;line-height:24px;font-weight:700;margin-bottom:8px">What the engine did</div>
      <div class="row" style="gap:8px;flex-wrap:wrap">
        <span class="badge" style="background:rgba(61,220,151,.14);color:#3DDC97;border-color:rgba(61,220,151,.4)">${icon('sparkles', 12, '#3DDC97', 2.2)}Noise −${noiseCut}%</span>
        <span class="badge" style="background:rgba(255,122,69,.14);color:#FF7A45;border-color:rgba(255,122,69,.4)">${icon('layers', 12, '#FF7A45', 2.2)}${report.framesUsed}/${report.framesInput} frames fused</span>
        <span class="badge" style="background:rgba(245,198,107,.16);color:${GOLD};border-color:rgba(245,198,107,.45)">${icon('crown', 12, GOLD, 2.2)}2× resolution</span>
      </div>
    </div>
  </div>`;

// ---- render ----
const page = (inner, w, h) =>
  `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body style="width:${w}px;height:${h}px">${inner}</body></html>`;
const browser = await chromium.launch();
const ctx = await browser.newContext({ deviceScaleFactor: 2 });
const p = await ctx.newPage();
const screens = [
  ['photo', 'Photo · tap-to-focus', photoMode],
  ['pro', 'Pro · shutter dial · focus peaking', proMode],
  ['night', 'Night · burst fusion in progress', nightMode],
  ['compare', 'Photo preview · before / after', compare],
];
for (const [key, , html] of screens) {
  await p.setViewportSize({ width: 390, height: 844 });
  await p.setContent(page(`<div class="phone" style="border-radius:0">${html}</div>`, 390, 844));
  await p.screenshot({ path: join(out, `phase2-${key}.png`) });
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
  <div style="font-size:13px;color:rgba(247,245,251,.74)">Phase 2 · Professional Camera · real engine output (Kodak test image, simulated weak camera)</div></div>
  <div style="display:flex;gap:36px">${frames}</div></div>`;
await p.setViewportSize({ width: W, height: 1050 });
await p.setContent(page(overview, W, 1050));
await p.screenshot({ path: join(out, 'phase2-overview.png'), fullPage: true });
await browser.close();
console.log('Phase 2 preview rendered to docs/design');
