// The Rewards share campaign's pictures (android/app/src/main/assets/promo):
//   post.jpg    1080 x 1350  a social post (feeds)
//   status.jpg  1080 x 1920  a status, story or update
//   friends.jpg 1080 x 1080  a message to friends
// Made from the app's own brand, fonts and studio scenes (no people, no
// third-party photos). Run from the repo root after changing the copy:
//   node tools/promo/render-promo.mjs
// (needs Playwright with Chromium).
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join } from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = process.cwd();
const assets = join(root, 'android/app/src/main/assets');
const out = join(assets, 'promo');
const b64 = (f, type) => `data:${type};base64,${readFileSync(join(assets, f)).toString('base64')}`;
const plate = id => b64(`studio/plates/${id}.jpg`, 'image/jpeg');
const emblem = b64('brand/emblem.png', 'image/png');
const font = f => b64(`fonts/${f}`, 'font/ttf');

// Icons from the app's own set (src/components/Icon.tsx).
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
const icon = (name, size, color = '#fff', sw = 1.9) =>
  `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round">${ICONS[name] ?? ''}</svg>`;

const GOLD = 'linear-gradient(135deg,#F7D58B 0%,#E9B04F 55%,#C98A2E 100%)';
const ACTION = 'linear-gradient(135deg,#FF7A45 0%,#E0408A 50%,#7445E0 100%)';

const css = `
@font-face{font-family:Poppins;src:url(${font('Poppins-SemiBold.ttf')})}
@font-face{font-family:PoppinsXB;src:url(${font('Poppins-ExtraBoldItalic.ttf')})}
@font-face{font-family:Bebas;src:url(${font('BebasNeue-Regular.ttf')})}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Poppins,system-ui,sans-serif;color:#F7F5FB;-webkit-font-smoothing:antialiased}
.card{position:relative;overflow:hidden;background:#07060D}
.glow{position:absolute;border-radius:50%;filter:blur(80px)}
.row{display:flex;align-items:center}
.col{display:flex;flex-direction:column}
.trial{display:inline-flex;align-items:center;gap:14px;padding:16px 30px;border-radius:999px;background-image:${GOLD};color:#1A1206;font-family:PoppinsXB;letter-spacing:.5px}
.play{display:inline-flex;align-items:center;gap:16px;padding:14px 26px;border-radius:20px;background:#000;border:2px solid rgba(255,255,255,.35)}
.feat{display:flex;align-items:center;gap:18px}
.fi{flex:none;display:flex;align-items:center;justify-content:center;border-radius:20px;background-image:${ACTION}}
.tag{color:rgba(247,245,251,.55)}
`;

// The celebration: confetti and sparkles in the brand's colours (fixed
// positions, so the pictures come out the same every time).
const CONFETTI = ['#FF7A45', '#F7D58B', '#E0408A', '#7445E0', '#2DD4BF', '#FFFFFF'];
function confetti(w, h, n, seed) {
  let x = seed;
  const rnd = () => (x = (x * 9301 + 49297) % 233280) / 233280;
  return Array.from({ length: n }, (_, i) => {
    const size = 10 + rnd() * 16;
    const round = i % 3 === 0;
    // Along the sides, clear of the words in the middle.
    const side = rnd();
    const left = side < 0.5 ? rnd() * w * 0.1 : w * 0.88 + rnd() * w * 0.11;
    return `<div style="position:absolute;left:${left.toFixed(0)}px;top:${(rnd() * h).toFixed(0)}px;width:${round ? size * 0.7 : size * 0.45}px;height:${round ? size * 0.7 : size}px;border-radius:${round ? '50%' : '3px'};background:${CONFETTI[i % CONFETTI.length]};opacity:${(0.55 + rnd() * 0.45).toFixed(2)};transform:rotate(${(rnd() * 180).toFixed(0)}deg)"></div>`;
  }).join('');
}
const star = (x, y, s, c = '#F7D58B') =>
  `<svg style="position:absolute;left:${x}px;top:${y}px" width="${s}" height="${s}" viewBox="0 0 24 24"><path d="M12 0c.8 6.6 4.6 10.4 12 12-7.4 1.6-11.2 5.4-12 12-.8-6.6-4.6-10.4-12-12C7.4 10.4 11.2 6.6 12 0z" fill="${c}"/></svg>`;
/** The headline, in celebration: "You can now photoshoot right on your phone!" */
const headline = (
  size,
  align = 'left',
) => `<div class="col" style="gap:${size * 0.22}px;align-items:${align === 'center' ? 'center' : 'flex-start'};text-align:${align}">
    <div style="display:inline-flex;align-items:center;gap:10px;padding:${size * 0.1}px ${size * 0.3}px;border-radius:999px;background-image:${ACTION};font-family:PoppinsXB;font-size:${size * 0.34}px;letter-spacing:2px;box-shadow:0 10px 30px -8px rgba(224,64,138,.7)">🎉 IT'S HERE! 🎉</div>
    <div style="font-family:PoppinsXB;font-size:${size}px;line-height:1.04;letter-spacing:-1px">You can now<br><span style="background-image:linear-gradient(90deg,#FFD36E 0%,#FF7A45 35%,#E0408A 70%,#A57BFF 100%);-webkit-background-clip:text;color:transparent">photoshoot</span><br>right on your phone! 📸</div>
  </div>`;

// A small Google Play mark (the coloured triangle), drawn here.
const playMark = s =>
  `<svg width="${s}" height="${s}" viewBox="0 0 48 48"><path d="M8 4l22 20L8 44c-1.2-.6-2-1.8-2-3.3V7.3C6 5.8 6.8 4.6 8 4z" fill="#00D7FE"/><path d="M30 24L8 4c.9-.5 2.1-.5 3.2.1l25.6 14.6z" fill="#00F076"/><path d="M30 24l6.8 5.3-25.6 14.6c-1.1.6-2.3.6-3.2.1z" fill="#F8364B"/><path d="M36.8 18.7l6 3.4c2.2 1.2 2.2 3.4 0 4.7l-6 3.4L30 24z" fill="#FFD400"/></svg>`;
const play = scale =>
  `<div class="play" style="transform-origin:left center;transform:scale(${scale})">${playMark(46)}<div class="col"><span style="font-size:15px;letter-spacing:1px;color:#ddd">GET IT ON</span><span style="font-size:30px;line-height:32px">Google Play</span></div></div>`;
const brand = (s, t) =>
  `<div class="row" style="gap:${s * 0.3}px"><img src="${emblem}" style="width:${s}px;height:${s}px;border-radius:${s * 0.24}px"><span style="font-family:PoppinsXB;font-size:${t}px;letter-spacing:.5px">Photo Plus</span></div>`;

const FEATURES = [
  ['layers', 'Photoshoot Studio', 'Be in a real studio, park or café — without leaving home'],
  ['sun', 'Relight', 'Pro studio lighting on any photo, in one tap'],
  ['bag', 'Product Studio', 'Clean catalogue shots that sell, with real shadows'],
  ['sparkles', 'Beauty & Spotlight', 'Flawless, natural skin and dreamy portrait blur'],
  ['camera', 'Lively camera', 'Rich, flagship-level colour in every photo'],
  ['film', 'Glide & Film', 'Smooth pro video, film strips and collages'],
];
const feature = ([ic, title, text], s) =>
  `<div class="feat"><div class="fi" style="width:${s}px;height:${s}px">${icon(ic, s * 0.5)}</div><div class="col" style="gap:2px"><span style="font-size:${s * 0.42}px;line-height:1.15">${title}</span><span style="font-size:${s * 0.3}px;line-height:1.25;color:rgba(247,245,251,.7)">${text}</span></div></div>`;

// A phone showing the studio: a scene behind a placeholder figure is avoided; the app's own tool grid instead.
const phone = (w, scene) => {
  const h = w * 2.05;
  const tile = (ic, t) =>
    `<div class="col" style="align-items:center;justify-content:center;gap:${w * 0.02}px;aspect-ratio:1;border-radius:${w * 0.06}px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.14)"><div style="width:${w * 0.13}px;height:${w * 0.13}px;border-radius:${w * 0.04}px;background-image:${GOLD};display:flex;align-items:center;justify-content:center">${icon(ic, w * 0.07, '#1A1206')}</div><span style="font-size:${w * 0.038}px;text-align:center;line-height:1.1">${t}</span></div>`;
  return `<div style="position:relative;width:${w}px;height:${h}px;border-radius:${w * 0.14}px;background:#111;padding:${w * 0.03}px;box-shadow:0 40px 90px -20px rgba(0,0,0,.8),0 0 0 2px rgba(255,255,255,.12)">
    <div style="width:100%;height:100%;border-radius:${w * 0.11}px;overflow:hidden;background:#07060D;display:flex;flex-direction:column;padding:${w * 0.06}px;gap:${w * 0.04}px">
      <span style="font-size:${w * 0.07}px">Photoshoot Studio</span>
      <img src="${plate(scene)}" style="width:100%;height:${h * 0.28}px;object-fit:cover;border-radius:${w * 0.06}px">
      <div style="display:grid;grid-template-columns:repeat(3,1fr);gap:${w * 0.03}px">${tile('layers', 'Scene Swap')}${tile('film', 'Film & Collage')}${tile('sun', 'Relight')}${tile('bag', 'Product Studio')}${tile('wand', 'Batch edit')}${tile('sparkles', 'Beauty')}</div>
    </div></div>`;
};

const strip = (ids, h, r) =>
  `<div class="row" style="gap:12px">${ids.map(id => `<img src="${plate(id)}" style="width:${h * 0.8}px;height:${h}px;object-fit:cover;border-radius:${r}px">`).join('')}</div>`;

// ---- post (4:5)
const post = `<div class="card" style="width:1080px;height:1350px;padding:70px 70px 60px">
  <div class="glow" style="width:900px;height:900px;left:-300px;top:-380px;background:#FF7A45;opacity:.33"></div>
  <div class="glow" style="width:900px;height:900px;right:-360px;bottom:-300px;background:#6B4BFF;opacity:.38"></div>
  ${confetti(1080, 560, 46, 7)}${star(930, 150, 54)}${star(990, 300, 30, '#fff')}${star(560, 120, 26)}
  <div class="col" style="position:relative;height:100%;gap:34px">
    ${brand(84, 50)}
    ${headline(68)}
    <div class="row" style="gap:44px;align-items:flex-start">
      <div class="col" style="flex:1;gap:20px">${FEATURES.slice(0, 5)
        .map(f => feature(f, 66))
        .join('')}</div>
      ${phone(300, 'white-studio')}
    </div>
    <div class="row" style="gap:26px;margin-top:auto;justify-content:space-between">
      <div class="trial" style="font-size:36px">${icon('crown', 40, '#1A1206', 2.2)}7 DAYS FREE TRIAL</div>
      ${play(1)}
    </div>
    <div class="tag" style="font-size:24px;text-align:center">No studio. No laptop. No experience needed.</div>
  </div></div>`;

// ---- status (9:16)
const status = `<div class="card" style="width:1080px;height:1920px;padding:110px 80px 90px">
  <div class="glow" style="width:1100px;height:1100px;left:-380px;top:-400px;background:#FF7A45;opacity:.33"></div>
  <div class="glow" style="width:1100px;height:1100px;right:-420px;bottom:-200px;background:#6B4BFF;opacity:.4"></div>
  ${confetti(1080, 820, 60, 11)}${star(90, 330, 50)}${star(930, 300, 60)}${star(860, 560, 30, '#fff')}${star(150, 610, 28, '#fff')}
  <div class="col" style="position:relative;height:100%;gap:44px;align-items:center;text-align:center">
    ${brand(110, 64)}
    ${headline(84, 'center')}
    ${strip(['sunlit-loft', 'white-studio', 'coastal-dusk'], 300, 26)}
    <div class="col" style="gap:30px;align-items:flex-start;text-align:left;width:100%">${FEATURES.slice(0, 4)
      .map(f => feature(f, 84))
      .join('')}</div>
    <div class="trial" style="font-size:44px;margin-top:auto">${icon('crown', 48, '#1A1206', 2.2)}7 DAYS FREE TRIAL</div>
    ${play(1.25)}
    <div class="tag" style="font-size:28px">Download free on Google Play</div>
  </div></div>`;

// ---- friends (1:1)
const friends = `<div class="card" style="width:1080px;height:1080px;padding:70px">
  <div class="glow" style="width:800px;height:800px;left:-260px;top:-300px;background:#FF7A45;opacity:.35"></div>
  <div class="glow" style="width:800px;height:800px;right:-300px;bottom:-280px;background:#6B4BFF;opacity:.4"></div>
  ${confetti(1080, 460, 38, 5)}${star(900, 140, 50)}${star(980, 300, 28, '#fff')}${star(640, 100, 24)}
  <div class="col" style="position:relative;height:100%;gap:30px">
    ${brand(76, 44)}
    ${headline(62)}
    <div style="font-size:28px;line-height:1.35;color:rgba(247,245,251,.82)">New backgrounds, studio lighting, beauty retouch and product photos that sell — all on your phone, in seconds.</div>
    ${strip(['white-studio', 'boho-corner', 'sunrise-beach', 'neon-lines'], 210, 22)}
    <div class="row" style="gap:24px;margin-top:auto;justify-content:space-between">
      <div class="trial" style="font-size:32px">${icon('crown', 36, '#1A1206', 2.2)}7 DAYS FREE TRIAL</div>
      ${play(0.95)}
    </div>
  </div></div>`;

const browser = await chromium.launch();
for (const [name, html, w, h] of [
  ['post', post, 1080, 1350],
  ['status', status, 1080, 1920],
  ['friends', friends, 1080, 1080],
]) {
  const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
  await page.setContent(
    `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body>${html}</body></html>`,
    {
      waitUntil: 'load',
    },
  );
  await page.screenshot({
    path: join(out, `${name}.jpg`),
    type: 'jpeg',
    quality: 90,
    clip: { x: 0, y: 0, width: w, height: h },
  });
  await page.close();
  console.log(`${name}.jpg`);
}
await browser.close();
