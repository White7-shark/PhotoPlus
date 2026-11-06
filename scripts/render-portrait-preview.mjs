#!/usr/bin/env node
/**
 * Renders the Phase 5 portrait design preview (docs/design/portrait-*.png)
 * from real engine output on Kodak test photos:
 *
 *   python3 scripts/validate-portrait.py portrait_demo face_landmarker.task segmenter.tflite OUT kodim04.png kodim15.png
 *   (then portrait_demo runs for the Spotlight background and full Advanced
 *    settings: see docs/PHASE5_REPORT.md, "How the preview was made")
 *   node scripts/render-portrait-preview.mjs ASSET_DIR SUMMARY.json
 *
 * Layout mirrors src/screens/editor/EditorScreen.tsx and PortraitPanel.tsx;
 * icons come from src/components/Icon.tsx.
 */
import { readFileSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const [assets, summaryFile] = process.argv.slice(2);
if (!assets || !summaryFile) {
  console.error('usage: node scripts/render-portrait-preview.mjs ASSET_DIR SUMMARY.json');
  process.exit(1);
}
const summary = JSON.parse(readFileSync(summaryFile, 'utf8'));
const out = join(root, 'docs/design');
mkdirSync(out, { recursive: true });
const uri = file => `data:image/png;base64,${readFileSync(join(assets, file)).toString('base64')}`;

// ---- icons parsed from Icon.tsx ----
const iconSrc = readFileSync(join(root, 'src/components/Icon.tsx'), 'utf8');
const body = iconSrc.slice(iconSrc.indexOf('export const ICONS = {'), iconSrc.indexOf('} as const;'));
const ICONS = {};
for (const m of body.matchAll(/\n {2}(\w+): ([\s\S]*?)(?=,\n {2}\w+: |,\n$)/g)) {
  ICONS[m[1]] = m[2]
    .replace(/<\/?>/g, '')
    .replace(/<Path/g, '<path')
    .replace(/<Circle/g, '<circle')
    .replace(/<Rect/g, '<rect')
    .replace(/=\{([^}]+)\}/g, '="$1"')
    .replace(/^\s*\(\s*|\s*\)\s*$/g, '');
}
const icon = (name, size = 18, color = '#FFFFFF', sw = 2) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round">${ICONS[name] ?? ''}</svg>`;

const GOLD = '#F5C66B';
const ACCENT = '#FF7A45';
const STROKE = 'rgba(255,255,255,0.14)';
const ACTION = 'linear-gradient(135deg, #CC4128 0%, #C2306A 50%, #7445E0 100%)';
const SHEEN =
  'linear-gradient(160deg, rgba(255,255,255,0.20) 0%, rgba(255,255,255,0.04) 38%, rgba(255,255,255,0.0) 60%, rgba(255,255,255,0.05) 100%)';
const css = `
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Roboto,'Segoe UI',system-ui,sans-serif;background:#0d0c12;color:#F7F5FB;-webkit-font-smoothing:antialiased}
.phone{position:relative;width:390px;min-height:844px;overflow:hidden;background:#05050A}
.status{position:absolute;top:0;left:0;right:0;height:44px;display:flex;justify-content:space-between;align-items:center;padding:0 26px;font-size:13px;font-weight:600;z-index:5;color:#fff}
.row{display:flex;align-items:center}
.caption{font-size:12px;line-height:16px;font-weight:500;letter-spacing:.2px}
.sec{color:rgba(247,245,251,.62)}
.overline{font-size:11px;line-height:14px;font-weight:700;letter-spacing:1.4px;text-transform:uppercase}
.glass{position:relative;background-color:rgba(255,255,255,0.065);border:1px solid ${STROKE};background-image:${SHEEN};box-shadow:0 18px 40px -12px rgba(0,0,0,0.45);overflow:hidden;border-radius:24px}
.btn{height:48px;border-radius:24px;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;font-size:15px}
table{border-collapse:collapse;width:100%}td,th{padding:7px 10px;border-bottom:1px solid ${STROKE};text-align:right;font-size:13px}th{color:rgba(247,245,251,.62);font-weight:600}td:first-child,th:first-child{text-align:left}
`;
const status = `<div class="status"><span>9:41</span><span>●●● ▮</span></div>`;
const aurora = `<div style="position:absolute;inset:0;background:#05050A">
  <div style="position:absolute;top:-170px;left:-150px;width:460px;height:460px;border-radius:50%;filter:blur(40px);opacity:.4;background-image:radial-gradient(circle,#FF7A45 0%,#FF7A45 18%,transparent 70%)"></div>
  <div style="position:absolute;bottom:-220px;left:10%;width:480px;height:480px;border-radius:50%;filter:blur(40px);opacity:.35;background-image:radial-gradient(circle,#5B5BF7 0%,#5B5BF7 18%,transparent 70%)"></div></div>`;
const round = (name, dim = false) =>
  `<div style="width:40px;height:40px;border-radius:20px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.065);border:1px solid ${STROKE};opacity:${dim ? 0.4 : 1}">${icon(name, 18, '#fff', 1.75)}</div>`;
const header = `<div class="row" style="gap:10px;margin-bottom:4px">${round('chevronLeft')}<div style="flex:1;font-size:20px;line-height:26px;font-weight:700">Edit</div>${round('undo')}${round('redo', true)}</div>`;

// Tool tabs, as EDITOR_TOOLS in panels.tsx (Retouch is now Portrait).
const TOOLS = [
  ['adjust', 'Adjust', 'sliders'],
  ['crop', 'Crop', 'aspect'],
  ['looks', 'Looks', 'image'],
  ['spotlight', 'Spotlight', 'target'],
  ['retouch', 'Portrait', 'sparkles'],
  ['focus', 'Focus', 'exposure'],
  ['transform', 'Transform', 'aperture', true],
];
const tools = active =>
  `<div style="overflow:hidden;flex:none"><div class="row" style="gap:8px;transform:translateX(-170px)">${TOOLS.map(
    ([id, label, ic, premium]) =>
      `<div style="flex:none;display:flex;flex-direction:column;align-items:center;gap:4px;padding:8px 12px;border-radius:14px;${id === active ? 'background:rgba(255,122,69,.16)' : ''}">${icon(ic, 20, id === active ? ACCENT : '#fff')}<div class="row caption" style="gap:3px">${label}${premium ? icon('crown', 11, GOLD, 2.2) : ''}</div></div>`,
  ).join('')}</div></div>`;
const photo = src =>
  `<div style="position:relative;width:100%;aspect-ratio:0.9;border-radius:18px;overflow:hidden;background:#000"><img src="${src}" style="width:100%;height:100%;object-fit:contain;display:block"></div>`;
const saveRow = `<div class="row" style="gap:10px"><div class="row caption" style="gap:6px;height:48px;padding:0 14px;border-radius:22px;border:1px solid ${STROKE}">${icon('compare', 16)}Original</div><div class="btn" style="flex:1;background-image:${ACTION}">${icon('check', 18)}Save</div></div>`;
const slider = (label, value, t, from = 0) => {
  const a = Math.min(from, t),
    b = Math.max(from, t);
  return `<div style="display:flex;flex-direction:column;gap:6px"><div class="row caption" style="justify-content:space-between"><span class="sec">${label}</span><span>${value}</span></div><div style="position:relative;height:26px;display:flex;align-items:center"><div style="width:100%;height:4px;border-radius:2px;background:${STROKE}"></div><div style="position:absolute;left:${a * 100}%;width:${(b - a) * 100}%;height:4px;border-radius:2px;background:${ACCENT}"></div><div style="position:absolute;left:calc(${t * 100}% - 11px);width:22px;height:22px;border-radius:11px;background:#fff;border:2px solid ${ACCENT}"></div></div></div>`;
};
const segmented = (label, options, active) =>
  `<div style="display:flex;flex-direction:column;gap:6px"><span class="caption sec">${label}</span><div class="row" style="padding:3px;border-radius:14px;background:rgba(255,255,255,.06);border:1px solid ${STROKE}">${options
    .map(
      o =>
        `<div class="caption" style="flex:1;text-align:center;padding:7px 0;border-radius:11px;font-weight:700;${o === active ? 'background:rgba(255,255,255,.16)' : 'color:rgba(247,245,251,.7)'}">${o}</div>`,
    )
    .join('')}</div></div>`;
const section = t => `<div class="overline sec" style="margin-top:4px">${t}</div>`;
const ghost = (label, ic) => `<div class="btn caption" style="height:40px;gap:6px;font-size:14px">${icon(ic, 16)}${label}</div>`;
const screen = (img, active, panel) =>
  `${aurora}${status}<div style="position:relative;padding:52px 16px 32px;display:flex;flex-direction:column;gap:12px">${header}${photo(img)}${saveRow}${tools(active)}<div class="glass" style="padding:14px 16px;display:flex;flex-direction:column;gap:10px">${panel}</div></div>`;

// Basic: the free Glow presets (kodim04, Natural).
const basic = screen(
  uri('kodim04_glow_natural.png'),
  'retouch',
  `${segmented('Controls', ['Basic', 'Advanced'], 'Basic')}<div class="sec" style="font-size:14px;line-height:20px">Glow smooths skin, clears blemishes and evens tone, keeping each person’s own colour.</div>${segmented('Glow', ['Off', 'Natural', 'Smooth', 'Flawless'], 'Natural')}${ghost('Restore original', 'undo')}`,
);

// Advanced (Premium): every control on its own (kodim04, the values shown).
const advanced = screen(
  uri('kodim04_full.png'),
  'retouch',
  `${segmented('Controls', ['Basic', 'Advanced'], 'Advanced')}${section('Skin')}${slider('Smooth', '60', 0.6)}${slider('Blemishes', '100', 1)}${slider('Even tone', '60', 0.6)}${slider('Shine', '60', 0.6)}${slider('Under-eyes', '70', 0.7)}${section('Face')}${slider('Eyes', '70', 0.7)}${slider('Teeth', '70', 0.7)}${slider('Lips', '50', 0.5)}${slider('Slim', '0', 0)}${section('Light')}${slider('Face light', '50', 0.5)}${slider('Studio light', '60', 0.6)}${slider('Light from', 'Left 60', 0.2, 0.5)}<div class="btn glass" style="border-radius:24px;height:44px">${icon('refresh', 18)}Reset to Glow</div>${ghost('Restore original', 'undo')}`,
);

// Spotlight with the Premium background controls (kodim15, black-and-white background, refined edges).
const background = screen(
  uri('kodim15_pop.png'),
  'spotlight',
  `<div class="sec" style="font-size:14px;line-height:20px">Tap what should shine. Untapped, the people (or the main subject) stand out.</div>${segmented('Background blur', ['Off', 'Light', 'Medium', 'Strong'], 'Medium')}${segmented('Glow', ['Off', 'Natural', 'Smooth', 'Flawless'], 'Natural')}${section('Background')}${slider('Blur amount', '60', 0.556)}${slider('Background light', '0', 0.5, 0.5)}${slider('Background colour', 'B&W', 0, 0.5)}${slider('Edge refinement', '100', 1)}`,
);

// ---- gallery: before / after on real photos, and the validation numbers ----
const W = 1240;
const cell = (file, label) =>
  `<div style="flex:1;display:flex;flex-direction:column;gap:8px"><div style="border-radius:14px;overflow:hidden;background:#000"><img src="${uri(file)}" style="width:100%;display:block"></div><div class="caption sec">${label}</div></div>`;
const row = (k, title) =>
  `<div style="font-size:16px;font-weight:700">${title}</div><div class="row" style="gap:12px;align-items:flex-start">${cell(`${k}_in.png`, 'Original')}${cell(`${k}_glow_natural.png`, 'Glow · Natural (free)')}${cell(`${k}_full.png`, 'Advanced: skin, eyes, teeth, lips, under-eyes, light')}${cell(`${k}_studio.png`, 'Studio light from the left')}${cell(`${k}_pop.png`, 'Spotlight: black-and-white background, refined edges')}</div>`;
const ORDER = ['very light', 'light', 'intermediate', 'tan', 'brown', 'dark'];
const fmt = v => v.toFixed(2);
const table = `<table><tr><th>Skin tone (measured, ITA)</th><th>Faces</th><th>Glow Natural ΔE</th><th>Glow Flawless ΔE</th><th>Advanced ΔE</th><th>Advanced lightness</th><th>Texture kept</th></tr>${ORDER.filter(
  t => summary[t],
)
  .map(t => {
    const g = summary[t];
    return `<tr><td>${t[0].toUpperCase()}${t.slice(1)}</td><td>${g.photos}</td><td>${fmt(g.glow_natural.toneDeltaE.median)}</td><td>${fmt(g.glow_flawless.toneDeltaE.median)}</td><td>${fmt(g.advanced.toneDeltaE.median)}</td><td>${g.advanced.lightnessChange.median >= 0 ? '+' : ''}${fmt(g.advanced.lightnessChange.median)} L*</td><td>${Math.round(100 * g.advanced.textureKept.median)}%</td></tr>`;
  })
  .join('')}</table>`;
const gallery = `<div style="padding:36px;display:flex;flex-direction:column;gap:18px">
  <div><div class="overline" style="color:${ACCENT}">Phase 5 · Portrait intelligence</div><div style="font-size:28px;font-weight:700;margin-top:4px">Natural portraits, every skin tone</div><div class="caption sec" style="margin-top:6px">Every image is the engine's real output on Kodak test photos (face landmarks and masks from an on-device-class model, as on the phone).</div></div>
  ${row('kodim04', 'Kodak 04')}
  ${row('kodim15', 'Kodak 15')}
  <div class="caption sec">A black-and-white background shows every mistake in the subject mask. The pink patch left beside the shoulder in Kodak 04 is where this desktop stand-in mask misjudged the background; on the phone the mask comes from on-device subject segmentation.</div>
  <div style="font-size:16px;font-weight:700;margin-top:6px">Measured on 49 faces across six skin-tone groups</div>
  <div class="caption sec">Median colour change of the cheeks' own tone (ΔE in a*, b*: under 2 is barely perceptible), lightness change and fine texture kept. A natural retouch keeps each person's own tone: the numbers are alike from very light to dark skin.</div>
  <div class="glass" style="padding:8px 14px">${table}</div>
</div>`;

const page = (inner, w, h) =>
  `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body style="width:${w}px;min-height:${h}px">${inner}</body></html>`;
const browser = await chromium.launch();
const p = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 });
for (const [key, html] of Object.entries({ basic, advanced, background })) {
  await p.setContent(page(`<div class="phone">${html}</div>`, 390, 844));
  await p.screenshot({ path: join(out, `portrait-${key}.png`), fullPage: true });
  console.log(`portrait-${key}.png`);
}
await p.setViewportSize({ width: W, height: 900 });
await p.setContent(page(gallery, W, 900));
await p.screenshot({ path: join(out, 'portrait-gallery.png'), fullPage: true });
console.log('portrait-gallery.png');
await browser.close();
