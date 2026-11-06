#!/usr/bin/env node
/**
 * Renders the Phase 3 photo editor design preview (docs/design/editor-*.png)
 * from real engine output:
 *
 *   python3 scripts/make-editor-assets.py KODAK_DIR edit_demo face_landmarker.task /tmp/pp-edit
 *   node scripts/render-editor-preview.mjs /tmp/pp-edit
 *
 * Layout mirrors src/screens/editor/EditorScreen.tsx (photo at 0.9 aspect,
 * Original + Save row, tool tabs, glass panel, Share / Save to gallery) and
 * the panels in panels.tsx and WatermarkPanel.tsx; icons come from
 * src/components/Icon.tsx. Every photo is engine output: the watermark,
 * frames, props and adjustments are not mocked.
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
  console.error('usage: node scripts/render-editor-preview.mjs ASSET_DIR');
  process.exit(1);
}
const out = join(root, 'docs/design');
mkdirSync(out, { recursive: true });

const uri = file => `data:image/png;base64,${readFileSync(join(assets, file)).toString('base64')}`;
const font = file =>
  `data:font/ttf;base64,${readFileSync(join(root, 'android/app/src/main/assets/fonts', file)).toString('base64')}`;

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
@font-face{font-family:Poppins;src:url(${font('Poppins-SemiBold.ttf')})}
@font-face{font-family:DMSerif;src:url(${font('DMSerifDisplay-Regular.ttf')})}
@font-face{font-family:GreatVibes;src:url(${font('GreatVibes-Regular.ttf')})}
@font-face{font-family:Bebas;src:url(${font('BebasNeue-Regular.ttf')})}
@font-face{font-family:SpaceMono;src:url(${font('SpaceMono-Regular.ttf')})}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Roboto,'Segoe UI',system-ui,sans-serif;background:#0d0c12;color:#F7F5FB;-webkit-font-smoothing:antialiased}
.phone{position:relative;width:390px;min-height:844px;overflow:hidden;background:#05050A}
.status{position:absolute;top:0;left:0;right:0;height:44px;display:flex;justify-content:space-between;align-items:center;padding:0 26px;font-size:13px;font-weight:600;z-index:5;color:#fff}
.row{display:flex;align-items:center}
.caption{font-size:12px;line-height:16px;font-weight:500;letter-spacing:.2px}
.sec{color:rgba(247,245,251,.62)}
.overline{font-size:11px;line-height:14px;font-weight:700;letter-spacing:1.4px;text-transform:uppercase}
.glass{position:relative;background-color:rgba(255,255,255,0.065);border:1px solid ${STROKE};background-image:${SHEEN};box-shadow:0 18px 40px -12px rgba(0,0,0,0.45);overflow:hidden;border-radius:24px}
.chip{height:32px;padding:0 12px;border-radius:16px;border:1px solid ${STROKE};display:inline-flex;align-items:center;gap:4px;font-size:12px;font-weight:700;flex:none;white-space:nowrap}
.chip.on{border-color:${ACCENT};background:rgba(255,122,69,.16);color:${ACCENT}}
.scroll{display:flex;gap:8px;overflow:hidden;white-space:nowrap}
.btn{height:48px;border-radius:24px;display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;font-size:15px}
`;

const status = `<div class="status"><span>9:41</span><span style="display:flex;gap:6px;align-items:center"><svg width="16" height="11" viewBox="0 0 16 11" fill="currentColor"><rect x="0" y="7" width="3" height="4" rx="1"/><rect x="4.3" y="5" width="3" height="6" rx="1"/><rect x="8.6" y="2.5" width="3" height="8.5" rx="1"/><rect x="13" y="0" width="3" height="11" rx="1"/></svg><svg width="24" height="12" viewBox="0 0 24 12" fill="none" stroke="currentColor"><rect x=".5" y=".5" width="20" height="11" rx="3"/><rect x="2" y="2" width="14" height="8" rx="1.5" fill="currentColor"/><path d="M22.5 4v4"/></svg></span></div>`;
const aurora = `<div style="position:absolute;inset:0;background:#05050A">
  <div style="position:absolute;top:-170px;left:-150px;width:460px;height:460px;border-radius:50%;filter:blur(40px);opacity:.4;background-image:radial-gradient(circle,#FF7A45 0%,#FF7A45 18%,transparent 70%)"></div>
  <div style="position:absolute;bottom:-220px;left:10%;width:480px;height:480px;border-radius:50%;filter:blur(40px);opacity:.35;background-image:radial-gradient(circle,#5B5BF7 0%,#5B5BF7 18%,transparent 70%)"></div></div>`;
const round = (name, dim = false) =>
  `<div style="width:40px;height:40px;border-radius:20px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.065);border:1px solid ${STROKE};opacity:${dim ? 0.4 : 1}">${icon(name, 18, '#fff', 1.75)}</div>`;
const header = `<div class="row" style="gap:10px;margin-bottom:4px">${round('chevronLeft')}<div style="flex:1;font-size:20px;line-height:26px;font-weight:700">Edit</div>${round('undo')}${round('redo', true)}</div>`;

const TOOLS = [
  ['adjust', 'Adjust', 'sliders'],
  ['crop', 'Crop', 'aspect'],
  ['looks', 'Looks', 'image'],
  ['spotlight', 'Spotlight', 'target'],
  ['retouch', 'Retouch', 'sparkles'],
  ['focus', 'Focus', 'exposure'],
  ['transform', 'Transform', 'aperture', true],
  ['props', 'Props', 'wand'],
  ['frames', 'Frames', 'grid'],
  ['watermark', 'Watermark', 'layers'],
];
const SHIFT = { adjust: 0, frames: -330, props: -260, watermark: -400 };
const tools = active =>
  `<div style="overflow:hidden;flex:none"><div class="row" style="gap:8px;transform:translateX(${SHIFT[active] ?? 0}px)">${TOOLS.map(
    ([id, label, ic, premium]) =>
      `<div style="flex:none;display:flex;flex-direction:column;align-items:center;gap:4px;padding:8px 12px;border-radius:14px;${id === active ? 'background:rgba(255,122,69,.16)' : ''}">${icon(ic, 20, id === active ? ACCENT : '#fff')}<div class="row caption" style="gap:3px">${label}${premium ? icon('crown', 11, GOLD, 2.2) : ''}</div></div>`,
  ).join('')}</div></div>`;

const photo = (src, extra = '') =>
  `<div style="position:relative;width:100%;aspect-ratio:0.9;border-radius:18px;overflow:hidden;background:#000"><img src="${src}" style="width:100%;height:100%;object-fit:contain;display:block">${extra}</div>`;
const saveRow = `<div class="row" style="gap:10px"><div class="row caption" style="gap:6px;height:48px;padding:0 14px;border-radius:22px;border:1px solid ${STROKE}">${icon('compare', 16)}Original</div><div class="btn" style="flex:1;background-image:${ACTION}">${icon('check', 18)}Save</div></div>`;
const exportRow = note =>
  `<div class="row" style="gap:10px"><div class="btn glass" style="flex:1;border-radius:24px">${icon('share', 18)}Share</div><div class="btn glass" style="flex:1;border-radius:24px">${icon('image', 18)}Save to gallery</div></div><div class="caption sec" style="text-align:center">${note}</div>`;

const slider = (label, value, t) =>
  `<div style="display:flex;flex-direction:column;gap:6px"><div class="row caption" style="justify-content:space-between"><span class="sec">${label}</span><span>${value}</span></div><div style="position:relative;height:28px;display:flex;align-items:center"><div style="width:100%;height:4px;border-radius:2px;background:${STROKE}"></div><div style="position:absolute;left:0;width:${t * 100}%;height:4px;border-radius:2px;background:${ACCENT}"></div><div style="position:absolute;left:calc(${t * 100}% - 11px);width:22px;height:22px;border-radius:11px;background:#fff;border:2px solid ${ACCENT}"></div></div></div>`;
const segmented = (label, options, active) =>
  `<div style="display:flex;flex-direction:column;gap:6px"><span class="caption sec">${label}</span><div class="row" style="padding:3px;border-radius:14px;background:rgba(255,255,255,.06);border:1px solid ${STROKE}">${options
    .map(
      o =>
        `<div class="caption" style="flex:1;text-align:center;padding:7px 0;border-radius:11px;font-weight:700;${o === active ? 'background:rgba(255,255,255,.16)' : 'color:rgba(247,245,251,.7)'}">${o}</div>`,
    )
    .join('')}</div></div>`;
const screen = (img, active, panel, note, extra = '') =>
  `${aurora}${status}<div style="position:relative;padding:52px 16px 32px;display:flex;flex-direction:column;gap:12px">${header}${photo(img, extra)}${saveRow}${tools(active)}<div class="glass" style="padding:14px 16px">${panel}</div>${exportRow(note)}</div>`;

// ---- catalogue (parsed from src/services/edit/catalog.ts) and preview tiles ----
const catalogSrc = readFileSync(join(root, 'src/services/edit/catalog.ts'), 'utf8');
const PROP_LIST = [
  ...catalogSrc.matchAll(/id: '(\w+)',\s*name: '([^']+)',\s*category: '(\w+)',\s*premium: (true|false)/g),
].map(m => ({ id: m[1], name: m[2], category: m[3], premium: m[4] === 'true' }));
const FRAME_LIST = [...catalogSrc.matchAll(/\{ id: '(\w+)', name: '([^']+)', premium: (true|false) \}/g)].map(m => ({
  id: m[1],
  name: m[2],
  premium: m[3] === 'true',
}));
const propUri = id =>
  `data:image/png;base64,${readFileSync(join(root, 'android/app/src/main/assets/props', `${id}.png`)).toString('base64')}`;
const tile = (label, src, { locked = false, selected = false, w = 76, h = 76, dark = false, ph = 'image' } = {}) =>
  `<div style="flex:none;width:${w}px;display:flex;flex-direction:column;align-items:center;gap:6px"><div style="position:relative;width:${w}px;height:${h}px;border-radius:14px;overflow:hidden;display:flex;align-items:center;justify-content:center;border:${selected ? 2 : 1}px solid ${selected ? ACCENT : STROKE};background:${dark ? 'rgba(10,9,17,.55)' : 'rgba(255,255,255,.065)'}">${src ? `<img src="${src}" style="max-width:${w - 10}px;max-height:${h - 10}px">` : icon(ph, 22, 'rgba(247,245,251,.56)')}${locked ? `<div style="position:absolute;right:5px;top:5px;width:20px;height:20px;border-radius:10px;background:rgba(10,9,17,.7);display:flex;align-items:center;justify-content:center">${icon('crown', 11, GOLD, 2.2)}</div>` : ''}</div><div class="caption" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:${w + 8}px">${label}</div></div>`;

const FREE_NOTE = 'Photos you share carry the Photo Plus mark. Your original stays untouched.';

// Adjust: kodim18 with warmth +25, the Golden Hour look at 70% and exposure +0.2, contrast +10 (make-editor-assets.py).
const adjust = screen(
  uri('edited.png'),
  'adjust',
  `<div style="display:flex;flex-direction:column;gap:6px">${slider('Exposure', '+0.2', 0.54)}${slider('Brightness', '0', 0.5)}${slider('Contrast', '+10', 0.55)}${slider('Warmth', '+25', 0.625)}</div>`,
  FREE_NOTE,
);

const frames = screen(
  uri('frame_arch.png'),
  'frames',
  `<div style="display:flex;flex-direction:column;gap:10px"><div class="scroll" style="gap:10px">${tile('None', null, { w: 68, h: 86, ph: 'close' })}${FRAME_LIST.slice(
    0,
    5,
  )
    .map(f => tile(f.name, uri(`frame_${f.id}.png`), { w: 68, h: 86, selected: f.id === 'arch', locked: f.premium }))
    .join(
      '',
    )}</div>${slider('Zoom', '1.0×', 0)}<div class="caption sec">${FRAME_LIST.length} frames, each shown around your own photo.</div></div>`,
  FREE_NOTE,
);

const props = screen(
  uri('glasses_round.png'),
  'props',
  `<div style="display:flex;flex-direction:column;gap:10px">${segmented('Category', ['Hats', 'Glasses', 'Outfit', 'Mine'], 'Glasses')}<div class="scroll" style="gap:10px">${PROP_LIST.filter(
    d => d.category === 'glasses',
  )
    .slice(0, 4)
    .map(d => tile(d.name, propUri(d.id), { dark: true, selected: d.id === 'round', locked: d.premium }))
    .join('')}</div><div class="caption sec">Drag to move · Fitted to the face</div></div>`,
  FREE_NOTE,
);

const STYLE_CHIPS = ['Classic', 'Pill', 'Signature', 'Minimal', 'Badge'];
const FONT_CHIPS = [
  ['Modern', 'Poppins'],
  ['Elegant', 'DMSerif'],
  ['Signature', 'GreatVibes'],
  ['Bold', 'Bebas'],
];
const posGrid = `<div style="display:grid;grid-template-columns:repeat(3,46px);gap:6px">${Array.from({ length: 9 }, (_, i) => `<div style="height:30px;border-radius:8px;border:1px solid ${i === 8 ? ACCENT : STROKE};background:${i === 8 ? 'rgba(255,122,69,.2)' : 'rgba(255,255,255,.05)'};display:flex;align-items:center;justify-content:center"><div style="width:8px;height:8px;border-radius:4px;background:${i === 8 ? ACCENT : 'rgba(247,245,251,.6)'}"></div></div>`).join('')}</div>`;
const watermark = screen(
  uri('wm_portrait.png'),
  'watermark',
  `<div style="display:flex;flex-direction:column;gap:10px"><div class="caption sec">Every shared photo carries the Photo Plus mark. Make it yours:</div><div class="scroll">${STYLE_CHIPS.map(s => `<div class="chip ${s === 'Signature' ? 'on' : ''}" style="border-radius:14px;height:36px">${s}</div>`).join('')}</div><div class="scroll">${FONT_CHIPS.map(([n, f], i) => `<div class="chip ${i === 0 ? 'on' : ''}" style="font-family:${f};font-weight:400;font-size:${f === 'GreatVibes' ? 18 : 14}px;height:34px;border-radius:14px">${n}</div>`).join('')}</div><div class="row" style="gap:14px;align-items:flex-end"><div style="flex:1">${slider('Size', '50', 0.5)}</div><div>${posGrid}</div></div></div>`,
  FREE_NOTE,
);

// ---- the camera with a prop on (it follows the face; the photo gets it in its pixels) ----
const camChip = (ic, on = false, tint = '#fff') =>
  `<div style="height:36px;min-width:40px;padding:0 10px;border-radius:18px;border:1px solid rgba(255,255,255,.16);background:${on ? 'rgba(255,255,255,.2)' : 'rgba(16,14,25,.55)'};display:flex;align-items:center;justify-content:center">${icon(ic, 18, tint)}</div>`;
const cameraProps = `<div style="position:absolute;inset:0;background:#000"></div>
  <div style="position:absolute;top:100px;left:0;width:390px;height:520px;overflow:hidden"><img src="${uri('camera_hearts.png')}" style="width:100%;height:100%;object-fit:cover;object-position:50% 30%"></div>
  ${status}
  <div class="row" style="position:absolute;top:52px;left:12px;right:12px;justify-content:space-between">${camChip('close')}<div class="row" style="gap:6px">${camChip('flashOff')}${camChip('timer')}${camChip('hat', true, GOLD)}${camChip('wand')}${camChip('grid', true)}${camChip('exposure')}</div></div>
  <div style="position:absolute;left:0;right:0;bottom:30px;display:flex;flex-direction:column;gap:10px">
    <div style="margin:0 12px;padding:12px;border-radius:22px;background:rgba(10,9,17,.66);display:flex;flex-direction:column;gap:10px">
      ${segmented('Props', ['Hats', 'Glasses', 'Outfit', 'Mine'], 'Glasses')}
      <div class="scroll" style="gap:10px">${tile('None', null, { w: 64, h: 64, ph: 'close' })}${PROP_LIST.filter(
        d => d.category === 'glasses',
      )
        .slice(3, 8)
        .map(d =>
          tile(d.name, propUri(d.id), { w: 64, h: 64, dark: true, selected: d.id === 'hearts', locked: d.premium }),
        )
        .join('')}</div>
      <div class="caption sec" style="text-align:center">It follows your face. Drag it to move it, pinch to resize.</div>
    </div>
    <div class="row" style="justify-content:space-between;padding:0 32px">
      <div style="width:64px;display:flex;flex-direction:column;align-items:center;gap:6px"><div style="width:50px;height:50px;border-radius:14px;overflow:hidden;border:1.5px solid rgba(255,255,255,.6)"><img src="${uri('camera_hearts.png')}" style="width:100%;height:100%;object-fit:cover"></div><div class="row caption" style="gap:4px;height:28px;padding:0 10px;border-radius:14px;border:1px solid rgba(255,255,255,.16);background:rgba(16,14,25,.55);font-weight:700">${icon('sliders', 14)}Edit</div></div>
      <div style="width:82px;height:82px;border-radius:41px;border:4px solid #fff;display:flex;align-items:center;justify-content:center"><div style="width:64px;height:64px;border-radius:32px;background-image:linear-gradient(135deg,#FF7A45 0%,#F2477E 48%,#8B5CF6 100%)"></div></div>
      ${camChip('flip')}
    </div>
  </div>`;

// ---- watching ads for a watermark-free photo (free plan) ----
const unlockCard = `<div class="glass" style="padding:14px 16px;display:flex;flex-direction:column;gap:8px"><div class="row" style="justify-content:space-between"><div style="font-weight:700;font-size:15px">Remove the watermark</div><div class="row" style="gap:5px">${[0, 1, 2, 3].map(i => `<div style="width:9px;height:9px;border-radius:5px;background:${i < 2 ? GOLD : STROKE}"></div>`).join('')}</div></div><div class="caption sec">2 more to go. Your progress is kept for this photo.</div><div class="btn glass" style="border-radius:24px">${icon('play', 18)}Watch ad 3 of 4</div></div>`;
const unlock = `${aurora}${status}<div style="position:relative;padding:52px 16px 32px;display:flex;flex-direction:column;gap:12px">${header}${photo(uri('wm_portrait.png'))}<div class="row" style="gap:10px"><div class="btn glass" style="flex:1;border-radius:24px">${icon('share', 18)}Share</div><div class="btn glass" style="flex:1;border-radius:24px">${icon('image', 18)}Save to gallery</div></div>${unlockCard}<div class="caption sec" style="text-align:center">${FREE_NOTE}</div></div>`;

// ---- making a prop (Premium): tap the object to cut it out ----
const marker = (x, y) =>
  `<div style="position:absolute;left:${x - 17}px;top:${y - 17}px;width:34px;height:34px;border-radius:17px;border:2px solid ${GOLD};background:rgba(10,9,17,.25);display:flex;align-items:center;justify-content:center"><div style="width:18px;height:18px;border-radius:9px;background:${GOLD};color:#1A1206;font-size:11px;font-weight:800;display:flex;align-items:center;justify-content:center">1</div></div>`;
const create = `${aurora}${status}<div style="position:relative;padding:52px 16px 32px;display:flex;flex-direction:column;gap:14px"><div class="row" style="gap:10px">${round('chevronLeft')}<div style="flex:1;font-size:20px;font-weight:700">Make a prop</div></div><div class="sec" style="font-size:15px;line-height:21px">Turn anything in a photo into a prop you can wear in the camera and the editor.</div>${photo(uri('kodim04.png'), marker(186, 70))}<div class="caption sec" style="text-align:center">Tap again to add more of it, or cut it out.</div><div class="row" style="gap:10px"><div class="btn glass" style="flex:1;border-radius:24px">${icon('image', 18)}Other photo</div><div class="btn" style="flex:1;background-image:${ACTION}">${icon('sparkles', 18)}Cut it out</div></div><div class="glass" style="padding:14px 16px;display:flex;flex-direction:column;gap:10px">${segmented('Where it goes', ['Head', 'Eyes', 'Neck', 'Anywhere'], 'Head')}<div class="caption sec">Saved props appear under Mine, in the camera and the editor.</div></div></div>`;

const page = (inner, w, h) =>
  `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body style="width:${w}px;min-height:${h}px">${inner}</body></html>`;

const browser = await chromium.launch();
const p = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 });
const SCREENS = { adjust, frames, props, watermark, unlock, create, camera: cameraProps };
for (const [key, html] of Object.entries(SCREENS)) {
  await p.setContent(page(`<div class="phone">${html}</div>`, 390, 844));
  await p.evaluate('document.fonts.ready');
  // The editor scrolls: the whole screen is captured, top to bottom.
  await p.screenshot({ path: join(out, `editor-${key}.png`), fullPage: true });
  console.log(`editor-${key}.png`);
}

// ---- gallery: every frame, and the watermark on light, dark and busy photos ----
const FRAMES = FRAME_LIST.map(f => [f.id, f.name, f.premium]);
const W = 1240;
const frameTile = ([id, name, premium]) =>
  `<div style="width:180px;display:flex;flex-direction:column;gap:8px"><div style="height:225px;border-radius:14px;overflow:hidden;background:#000;display:flex;align-items:center;justify-content:center"><img src="${uri(`frame_${id}.png`)}" style="max-width:100%;max-height:100%"></div><div class="row caption" style="gap:4px;font-weight:700">${name}${premium ? icon('crown', 12, GOLD, 2.2) : '<span class="sec" style="font-weight:500">· Free</span>'}</div></div>`;
const wmCrop = (file, label) =>
  `<div style="flex:1;display:flex;flex-direction:column;gap:8px"><div style="height:200px;border-radius:14px;overflow:hidden;position:relative;background:#000"><img src="${uri(file)}" style="position:absolute;right:0;bottom:0;width:190%"></div><div class="caption sec">${label}</div></div>`;
const gallery = `<div style="padding:36px;display:flex;flex-direction:column;gap:22px">
  <div><div class="overline" style="color:${ACCENT}">Phase 3 · Photo editor</div><div style="font-size:28px;font-weight:700;margin-top:4px">Frames and the Photo Plus mark</div><div class="caption sec" style="margin-top:6px">Every image is the engine's real output. Frames are drawn at full resolution; the mark is drawn into the photo's pixels and adapts its ink to what is behind it.</div></div>
  <div style="display:flex;flex-wrap:wrap;gap:16px">${FRAMES.map(frameTile).join('')}</div>
  <div style="font-size:18px;font-weight:700">Signature mark on free photos (bottom right, ink adapts)</div>
  <div class="row" style="gap:16px">${wmCrop('wm_dark.png', 'Busy, dark: white ink with a soft shadow')}${wmCrop('wm_street.png', 'Mid-tone street: white ink')}${wmCrop('wm_bright.png', 'Bright, high-key: deep charcoal ink')}</div>
</div>`;
await p.setViewportSize({ width: W, height: 900 });
await p.setContent(page(gallery, W, 900));
await p.screenshot({ path: join(out, 'editor-gallery.png'), fullPage: true });
console.log('editor-gallery.png');
await browser.close();
