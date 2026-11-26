// The Rewards share campaign's pictures, in each of the app's languages
// (android/app/src/main/assets/promo, and promo/<language> for each
// translation; the words are in tools/promo/copy.mjs):
//   post.jpg    1080 x 1350  a social post (feeds)
//   status.jpg  1080 x 1920  a status, story or update
//   friends.jpg 1080 x 1080  a message to friends
// Made from the app's own brand, fonts and studio scenes. Photos of people
// (a real photoshoot made with the app) work best: put photos you have the
// rights and the people's permission to use in tools/promo/people/*.jpg
// (kept out of the repository) and they take the studio scenes' place.
// Run from the repo root after changing the words:
//   node tools/promo/render-promo.mjs            (every language)
//   node tools/promo/render-promo.mjs es fr      (some)
// (needs Playwright with Chromium; fonts for Arabic, Hindi, Russian and
// Chinese are fetched from Google Fonts into tools/promo/.fonts once).
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync, readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join } from 'node:path';

import { COPY } from './copy.mjs';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = process.cwd();
const assets = join(root, 'android/app/src/main/assets');
const out = join(assets, 'promo');
const b64 = (f, type) => `data:${type};base64,${readFileSync(join(assets, f)).toString('base64')}`;
const plate = id => b64(`studio/plates/${id}.jpg`, 'image/jpeg');

// Photos of people, when given (tools/promo/people, not in the repository).
const peopleDir = join(root, 'tools/promo/people');
const PEOPLE = existsSync(peopleDir)
  ? readdirSync(peopleDir)
      .filter(f => /\.jpe?g$/i.test(f))
      .sort()
      .map(f => `data:image/jpeg;base64,${readFileSync(join(peopleDir, f)).toString('base64')}`)
  : [];
/** The i-th picture of a set: a person's photo when there are any, else the studio scene. */
const pic = (scene, i) => (PEOPLE.length ? PEOPLE[i % PEOPLE.length] : plate(scene));

// Fonts beyond Latin, from Google Fonts (cached; not in the repository).
const fontDir = join(root, 'tools/promo/.fonts');
function webFont(family, weight) {
  const file = join(fontDir, `${family.replace(/ /g, '')}-${weight}.ttf`);
  if (!existsSync(file)) {
    mkdirSync(fontDir, { recursive: true });
    const css = execFileSync('curl', [
      '-sfL',
      `https://fonts.googleapis.com/css2?family=${family.replace(/ /g, '+')}:wght@${weight}`,
    ]).toString();
    const url = /url\((https:[^)]+\.ttf)\)/.exec(css)?.[1];
    if (!url) throw new Error(`No font file for ${family} ${weight}`);
    execFileSync('curl', ['-sfL', '-o', file, url]);
  }
  return `data:font/ttf;base64,${readFileSync(file).toString('base64')}`;
}
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

// Poppins for Latin; Noto Sans for the rest of Latin and Cyrillic; the
// language's own script font after them (each letter takes the first font that has it).
const css = c => `
@font-face{font-family:Poppins;src:url(${font('Poppins-SemiBold.ttf')})}
@font-face{font-family:PoppinsXB;src:url(${font('Poppins-ExtraBoldItalic.ttf')})}
@font-face{font-family:NotoB;src:url(${webFont('Noto Sans', 700)})}
@font-face{font-family:NotoXB;src:url(${webFont('Noto Sans', 800)})}
${c.script && c.script !== 'Noto Sans' ? `@font-face{font-family:ScriptB;src:url(${webFont(c.script, 700)})}\n@font-face{font-family:ScriptXB;src:url(${webFont(c.script, 800)})}` : ''}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Poppins,NotoB,ScriptB,sans-serif;color:#F7F5FB;-webkit-font-smoothing:antialiased}
.xb{font-family:PoppinsXB,NotoXB,ScriptXB,sans-serif}
.card{position:relative;overflow:hidden;background:#07060D}
.glow{position:absolute;border-radius:50%;filter:blur(80px)}
.row{display:flex;align-items:center}
.col{display:flex;flex-direction:column}
.trial{display:inline-flex;align-items:center;gap:14px;padding:16px 30px;border-radius:999px;background-image:${GOLD};color:#1A1206;font-family:PoppinsXB,NotoXB,ScriptXB,sans-serif;letter-spacing:.5px;white-space:nowrap}
.play{display:inline-flex;align-items:center;gap:16px;padding:14px 26px;border-radius:20px;background:#000;border:2px solid rgba(255,255,255,.35);direction:ltr}
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
/** The headline, in celebration (the middle line in the gradient). */
const headline = (c, size, align = 'start') => {
  const [pre, hl, post] = c.headline;
  // Scripts with marks above and below the letters need more room between lines.
  const k = c.script && c.script !== 'Noto Sans' ? 1.24 : 1.04;
  return `<div class="col hl" style="gap:${size * 0.22}px;align-items:${align === 'center' ? 'center' : 'flex-start'};text-align:${align}">
    <div class="xb" style="display:inline-flex;align-items:center;gap:10px;padding:${size * 0.1}px ${size * 0.3}px;border-radius:999px;background-image:${ACTION};font-size:${size * 0.34}px;letter-spacing:${c.rtl ? 0 : 2}px;box-shadow:0 10px 30px -8px rgba(224,64,138,.7)">${c.ribbon}</div>
    <div class="xb" style="font-size:${size}px;line-height:${k};letter-spacing:${c.rtl ? 0 : -1}px">${pre}<br><span style="background-image:linear-gradient(90deg,#FFD36E 0%,#FF7A45 35%,#E0408A 70%,#A57BFF 100%);-webkit-background-clip:text;color:transparent;white-space:nowrap">${hl}</span><br><span style="white-space:nowrap">${post}</span></div>
  </div>`;
};

// A small Google Play mark (the coloured triangle), drawn here.
const playMark = s =>
  `<svg width="${s}" height="${s}" viewBox="0 0 48 48"><path d="M8 4l22 20L8 44c-1.2-.6-2-1.8-2-3.3V7.3C6 5.8 6.8 4.6 8 4z" fill="#00D7FE"/><path d="M30 24L8 4c.9-.5 2.1-.5 3.2.1l25.6 14.6z" fill="#00F076"/><path d="M30 24l6.8 5.3-25.6 14.6c-1.1.6-2.3.6-3.2.1z" fill="#F8364B"/><path d="M36.8 18.7l6 3.4c2.2 1.2 2.2 3.4 0 4.7l-6 3.4L30 24z" fill="#FFD400"/></svg>`;
const play = (c, scale) =>
  `<div class="play" style="transform-origin:${c.rtl ? 'right' : 'left'} center;transform:scale(${scale})">${playMark(46)}<div class="col"><span style="font-size:15px;letter-spacing:1px;color:#ddd">${c.playTop}</span><span style="font-size:30px;line-height:32px">Google Play</span></div></div>`;
const brand = (s, t) =>
  `<div class="row" style="gap:${s * 0.3}px"><img src="${emblem}" style="width:${s}px;height:${s}px;border-radius:${s * 0.24}px"><span class="xb" style="font-size:${t}px;letter-spacing:.5px">Photo Plus</span></div>`;

const FEATURES = c => [
  ['layers', 'Photoshoot Studio', c.features[0]],
  ['sun', 'Relight', c.features[1]],
  ['bag', 'Product Studio', c.features[2]],
  ['sparkles', 'Beauty & Spotlight', c.features[3]],
  ['camera', c.lively, c.features[4]],
  ['film', 'Glide & Film', c.features[5]],
];
const feature = ([ic, title, text], s) =>
  `<div class="feat"><div class="fi" style="width:${s}px;height:${s}px">${icon(ic, s * 0.5)}</div><div class="col" style="gap:2px"><span style="font-size:${s * 0.42}px;line-height:1.15">${title}</span><span style="font-size:${s * 0.3}px;line-height:1.25;color:rgba(247,245,251,.7)">${text}</span></div></div>`;

// A phone showing the studio: a scene behind a placeholder figure is avoided; the app's own tool grid instead.
const phone = (c, w, scene) => {
  const h = w * 2.05;
  const tile = (ic, t) =>
    `<div class="col" style="align-items:center;justify-content:center;gap:${w * 0.02}px;aspect-ratio:1;border-radius:${w * 0.06}px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.14)"><div style="width:${w * 0.13}px;height:${w * 0.13}px;border-radius:${w * 0.04}px;background-image:${GOLD};display:flex;align-items:center;justify-content:center">${icon(ic, w * 0.07, '#1A1206')}</div><span style="font-size:${w * 0.038}px;text-align:center;line-height:1.1">${t}</span></div>`;
  return `<div style="position:relative;width:${w}px;height:${h}px;border-radius:${w * 0.14}px;background:#111;padding:${w * 0.03}px;box-shadow:0 40px 90px -20px rgba(0,0,0,.8),0 0 0 2px rgba(255,255,255,.12)">
    <div style="width:100%;height:100%;border-radius:${w * 0.11}px;overflow:hidden;background:#07060D;display:flex;flex-direction:column;padding:${w * 0.06}px;gap:${w * 0.04}px">
      <span style="font-size:${w * 0.07}px">Photoshoot Studio</span>
      <img src="${pic(scene, 0)}" style="width:100%;height:${h * 0.28}px;object-fit:cover;object-position:center 25%;border-radius:${w * 0.06}px">
      <div style="display:grid;grid-template-columns:repeat(3,1fr);gap:${w * 0.03}px">${tile('layers', 'Scene Swap')}${tile('film', 'Film & Collage')}${tile('sun', 'Relight')}${tile('bag', 'Product Studio')}${tile('wand', c.batch)}${tile('sparkles', 'Beauty')}</div>
    </div></div>`;
};

// people: which photo of tools/promo/people each frame shows (by sorted name).
const strip = (ids, h, r, people = []) =>
  `<div class="row" style="gap:12px">${ids
    .map(
      (id, i) =>
        `<img src="${pic(id, people[i] ?? i)}" style="width:${h * 0.8}px;height:${h}px;object-fit:cover;object-position:center 22%;border-radius:${r}px;border:3px solid rgba(255,255,255,.9);box-shadow:0 18px 40px -12px rgba(0,0,0,.7)">`,
    )
    .join('')}</div>`;

// A phone in the camera, a portrait on its screen (when there are photos of people).
const phoneShot = (w, person) => {
  const h = w * 2.05;
  return `<div style="position:relative;width:${w}px;height:${h}px;border-radius:${w * 0.14}px;background:#111;padding:${w * 0.03}px;box-shadow:0 40px 90px -20px rgba(0,0,0,.8),0 0 0 2px rgba(255,255,255,.14)">
    <div style="position:relative;width:100%;height:100%;border-radius:${w * 0.11}px;overflow:hidden;background:#000">
      <img src="${PEOPLE[person % PEOPLE.length]}" style="position:absolute;inset:0;width:100%;height:100%;object-fit:cover;object-position:center 20%">
      <div style="position:absolute;left:0;right:0;top:0;height:${h * 0.12}px;background:linear-gradient(rgba(0,0,0,.55),transparent)"></div>
      <div style="position:absolute;left:0;right:0;bottom:0;height:${h * 0.22}px;background:linear-gradient(transparent,rgba(0,0,0,.7))"></div>
      <div style="position:absolute;left:50%;bottom:${h * 0.04}px;transform:translateX(-50%);width:${w * 0.22}px;height:${w * 0.22}px;border-radius:50%;border:${w * 0.015}px solid #fff;background:rgba(255,255,255,.25)"></div>
      <div style="position:absolute;top:${h * 0.035}px;left:50%;transform:translateX(-50%);padding:${w * 0.012}px ${w * 0.05}px;border-radius:999px;background-image:${ACTION};font-size:${w * 0.045}px;white-space:nowrap">Photoshoot Studio</div>
    </div></div>`;
};

// ---- post (4:5)
const post = c => `<div class="card" style="width:1080px;height:1350px;padding:70px 70px 60px">
  <div class="glow" style="width:900px;height:900px;left:-300px;top:-380px;background:#FF7A45;opacity:.33"></div>
  <div class="glow" style="width:900px;height:900px;right:-360px;bottom:-300px;background:#6B4BFF;opacity:.38"></div>
  ${confetti(1080, 560, 46, 7)}${star(930, 150, 54)}${star(990, 300, 30, '#fff')}${star(560, 120, 26)}
  <div class="col" style="position:relative;height:100%;gap:34px">
    ${brand(84, 50)}
    ${headline(c, 68)}
    <div class="row feats" style="gap:44px;align-items:flex-start">
      <div class="col" style="flex:1;gap:20px">${FEATURES(c)
        .slice(0, PEOPLE.length ? 3 : 5)
        .map(f => feature(f, 66))
        .join(
          '',
        )}${PEOPLE.length ? `<div style="margin-top:8px">${strip(['', '', ''], 196, 18, [1, 2, 4])}</div>` : ''}</div>
      ${PEOPLE.length ? phoneShot(300, 0) : phone(c, 300, 'white-studio')}
    </div>
    <div class="row" style="gap:26px;margin-top:auto;justify-content:space-between">
      <div class="trial" style="font-size:36px">${icon('crown', 40, '#1A1206', 2.2)}${c.trial}</div>
      ${play(c, 1)}
    </div>
    <div class="tag" style="font-size:24px;text-align:center">${c.tagline}</div>
  </div></div>`;

// ---- status (9:16)
const status = c => `<div class="card" style="width:1080px;height:1920px;padding:110px 80px 90px">
  <div class="glow" style="width:1100px;height:1100px;left:-380px;top:-400px;background:#FF7A45;opacity:.33"></div>
  <div class="glow" style="width:1100px;height:1100px;right:-420px;bottom:-200px;background:#6B4BFF;opacity:.4"></div>
  ${confetti(1080, 820, 60, 11)}${star(90, 330, 50)}${star(930, 300, 60)}${star(860, 560, 30, '#fff')}${star(150, 610, 28, '#fff')}
  <div class="col" style="position:relative;height:100%;gap:44px;align-items:center;text-align:center">
    ${brand(110, 64)}
    ${headline(c, 84, 'center')}
    ${strip(['sunlit-loft', 'white-studio', 'coastal-dusk'], 300, 26, [1, 3, 2])}
    <div class="col feats" style="gap:30px;align-items:flex-start;text-align:start;width:100%">${FEATURES(c)
      .slice(0, 4)
      .map(f => feature(f, 84))
      .join('')}</div>
    <div class="trial" style="font-size:44px;margin-top:auto">${icon('crown', 48, '#1A1206', 2.2)}${c.trial}</div>
    ${play(c, 1.25)}
    <div class="tag" style="font-size:28px">${c.download}</div>
  </div></div>`;

// ---- friends (1:1)
const friends = c => `<div class="card" style="width:1080px;height:1080px;padding:70px">
  <div class="glow" style="width:800px;height:800px;left:-260px;top:-300px;background:#FF7A45;opacity:.35"></div>
  <div class="glow" style="width:800px;height:800px;right:-300px;bottom:-280px;background:#6B4BFF;opacity:.4"></div>
  ${confetti(1080, 460, 38, 5)}${star(900, 140, 50)}${star(980, 300, 28, '#fff')}${star(640, 100, 24)}
  <div class="col" style="position:relative;height:100%;gap:30px">
    ${brand(76, 44)}
    ${headline(c, 62)}
    <div class="feats" style="font-size:28px;line-height:1.35;color:rgba(247,245,251,.82)">${c.friends}</div>
    ${strip(['white-studio', 'boho-corner', 'sunrise-beach', 'neon-lines'], PEOPLE.length ? 280 : 210, 22, [4, 6, 5, 1])}
    <div class="row" style="gap:24px;margin-top:auto;justify-content:space-between">
      <div class="trial" style="font-size:32px">${icon('crown', 36, '#1A1206', 2.2)}${c.trial}</div>
      ${play(c, 0.95)}
    </div>
  </div></div>`;

// Words differ in length from language to language: the gradient line is
// made to fit its width, then, when a picture's content runs over, the
// headline and the feature list are made a little smaller in turn until it fits.
const FIT = `(() => {
  const body = document.querySelector('.card > .col');
  const over = () => body.scrollHeight > body.clientHeight + 1 || body.scrollWidth > body.clientWidth + 1;
  const hl = document.querySelector('.hl');
  const feats = document.querySelector('.feats');
  const lines = [...hl.querySelectorAll('span')];
  const wide = () => lines.some(l => l.getBoundingClientRect().width > body.clientWidth);
  let z = 1;
  let f = 1;
  while (z > 0.6 && wide()) hl.style.zoom = z = +(z - 0.03).toFixed(2);
  // Then both, a little at a time, the larger first.
  while (over() && (z > 0.7 || f > 0.75)) {
    if (z >= f + 0.06 || f <= 0.75) hl.style.zoom = z = +(z - 0.03).toFixed(2);
    else feats.style.zoom = f = +(f - 0.03).toFixed(2);
  }
  return { headline: z, features: f, fits: !over() };
})()`;

const wanted = process.argv.slice(2);
const langs = Object.keys(COPY).filter(l => !wanted.length || wanted.includes(l));
const browser = await chromium.launch();
for (const lang of langs) {
  const c = COPY[lang];
  const dir = c.folder ? join(out, c.folder) : out;
  mkdirSync(dir, { recursive: true });
  for (const [name, html, w, h] of [
    ['post', post(c), 1080, 1350],
    ['status', status(c), 1080, 1920],
    ['friends', friends(c), 1080, 1080],
  ]) {
    const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
    await page.setContent(
      `<!doctype html><html lang="${lang}" dir="${c.rtl ? 'rtl' : 'ltr'}"><head><meta charset="utf-8"><style>${css(c)}</style></head><body>${html}</body></html>`,
      { waitUntil: 'load' },
    );
    await page.evaluate('document.fonts.ready');
    const fit = await page.evaluate(FIT);
    if (!fit.fits) console.warn(`${lang}/${name}: still too long for the picture`);
    await page.screenshot({
      path: join(dir, `${name}.jpg`),
      type: 'jpeg',
      quality: 86,
      clip: { x: 0, y: 0, width: w, height: h },
    });
    await page.close();
    console.log(`${c.folder || '.'}/${name}.jpg  headline ${fit.headline}  features ${fit.features}`);
  }
}
await browser.close();
