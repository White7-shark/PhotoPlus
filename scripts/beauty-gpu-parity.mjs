/**
 * Live-preview parity: runs the phone's beauty shaders (android assets
 * beauty/*.glsl, *.frag, *.vert — the same files) in headless Chromium's
 * WebGL2 on a real photo and compares them with the CPU engine's result for
 * the same recipe (beauty_demo). Development only.
 *
 *   node scripts/beauty-gpu-parity.mjs BEAUTY_DEMO WORK_DIR OUT_DIR [--grade-only] [--recipes RECIPES.txt]
 *
 * WORK_DIR is a photo folder from scripts/beauty-demo.py (in.ppm,
 * subject.pgm, faces.pgm, protect.pgm, landmarks.txt, scene.json).
 * Reports CIELAB difference (mean, 95th percentile) per preset, overall and
 * on skin, and writes side-by-side images (CPU | GPU).
 */
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { blurWeights, faceUniforms, LOW_SIGMA, lowSize, SMALL_SIGMA, studioMean } from './beauty-live.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const [demo, work, out] = process.argv.slice(2);
const gradeOnly = process.argv.includes('--grade-only');
mkdirSync(out, { recursive: true });

function readPnm(path) {
  const buf = readFileSync(path);
  let pos = 0;
  const token = () => {
    while (/\s/.test(String.fromCharCode(buf[pos]))) pos++;
    let s = '';
    while (!/\s/.test(String.fromCharCode(buf[pos]))) s += String.fromCharCode(buf[pos++]);
    return s;
  };
  const magic = token();
  const w = Number(token());
  const h = Number(token());
  token();
  pos++;
  const ch = magic === 'P6' ? 3 : 1;
  return { w, h, ch, data: buf.subarray(pos, pos + w * h * ch) };
}

function writePpm(path, w, h, rgb) {
  writeFileSync(path, Buffer.concat([Buffer.from(`P6\n${w} ${h}\n255\n`), Buffer.from(rgb)]));
}

// ---- CPU: the engine on the same recipes ----------------------------------
const recipeArgs = [
  '--experimental-strip-types',
  '--no-warnings',
  join(here, 'beauty-recipes.mts'),
  ...(gradeOnly ? ['--grade-only'] : []),
];
const custom = process.argv.indexOf('--recipes');
const recipesText =
  custom > 0 ? readFileSync(process.argv[custom + 1], 'utf8') : execFileSync('node', recipeArgs, { encoding: 'utf8' });
writeFileSync(join(out, 'recipes.txt'), recipesText);
const cpuLog = execFileSync(demo, [
  join(work, 'in.ppm'),
  join(work, 'subject.pgm'),
  join(work, 'faces.pgm'),
  join(work, 'protect.pgm'),
  join(work, 'landmarks.txt'),
  join(out, 'recipes.txt'),
  join(out, 'cpu_'),
], { encoding: 'utf8' });
const sceneLine = cpuLog.split('\n').find(l => l.startsWith('{"scene"'));
const scene = JSON.parse(sceneLine).scene;
const recipes = recipesText
  .trim()
  .split('\n')
  .map(l => {
    const [id, ...v] = l.split(' ');
    const n = v.map(Number);
    return { id, portrait: n.slice(0, 6), face: n.slice(6, 19), grade: n.slice(19) };
  });

// ---- inputs ---------------------------------------------------------------
const photo = readPnm(join(work, 'in.ppm'));
const mask = readPnm(join(work, 'subject.pgm'));
const { w: W, h: H } = photo;
const rgba = Buffer.alloc(W * H * 4);
for (let i = 0; i < W * H; i++) {
  rgba[i * 4] = photo.data[i * 3];
  rgba[i * 4 + 1] = photo.data[i * 3 + 1];
  rgba[i * 4 + 2] = photo.data[i * 3 + 2];
  rgba[i * 4 + 3] = 255;
}
const faces = readFileSync(join(work, 'landmarks.txt'), 'utf8')
  .trim()
  .split('\n')
  .filter(Boolean)
  .map(l => l.split(',').map(Number));
let face = null;
for (const f of faces) {
  const u = faceUniforms(f, W, H);
  if (!face || u.faceWidth > face.faceWidth) face = u;
}
const low = lowSize(W, H);
const lowBlur = blurWeights((LOW_SIGMA * Math.min(W, H)) / low.f);
const small = { w: Math.ceil(W / 2), h: Math.ceil(H / 2) };
const smallBlur = blurWeights((SMALL_SIGMA * Math.min(W, H)) / 2);
const shaderDir = join(here, '../android/app/src/main/assets/beauty');
const src = name => readFileSync(join(shaderDir, name), 'utf8');
const shaders = {
  common: src('common.glsl'),
  vert: src('fullscreen.vert'),
  down: src('down.frag'),
  blur: src('blur.frag'),
  composite: src('composite.frag'),
};

// ---- GPU: WebGL2 ----------------------------------------------------------
const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const page = await browser.newPage();
await page.setContent('<canvas id="c"></canvas>');
const results = {};
const setup = {
  W,
  H,
  low,
  lowBlur,
  small,
  smallBlur,
  shaders,
  scene,
  face,
  studioMean: studioMean(scene[7]),
  photo: rgba.toString('base64'),
  mask: Buffer.from(mask.data).toString('base64'),
  maskW: mask.w,
  maskH: mask.h,
};
const init = await page.evaluate(s => {
  const b64 = t => Uint8Array.from(atob(t), c => c.charCodeAt(0));
  const canvas = document.getElementById('c');
  canvas.width = s.W;
  canvas.height = s.H;
  const gl = canvas.getContext('webgl2', { preserveDrawingBuffer: true, antialias: false });
  if (!gl) return { error: 'no WebGL2' };
  const prefix = '#version 300 es\n#define CAMERA_SAMPLER sampler2D\n';
  const compile = (type, text) => {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, prefix + s.shaders.common + '\n' + text);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh));
    return sh;
  };
  const program = frag => {
    const p = gl.createProgram();
    gl.attachShader(p, compile(gl.VERTEX_SHADER, s.shaders.vert));
    gl.attachShader(p, compile(gl.FRAGMENT_SHADER, frag));
    gl.bindAttribLocation(p, 0, 'aPos');
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    return p;
  };
  let progs;
  try {
    progs = { down: program(s.shaders.down), blur: program(s.shaders.blur), comp: program(s.shaders.composite) };
  } catch (e) {
    return { error: String(e) };
  }
  const buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
  const tex = (w, h, fmt, data, flip = true) => {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    const internal = fmt === 'r' ? gl.R8 : gl.RGBA8;
    const format = fmt === 'r' ? gl.RED : gl.RGBA;
    // Rows go bottom-up in GL: upload flipped so vUv (bottom-left origin) maps directly.
    let rows = data && !flip ? data : null;
    if (data && flip) {
      const bpp = fmt === 'r' ? 1 : 4;
      rows = new Uint8Array(data.length);
      for (let y = 0; y < h; y++) rows.set(data.subarray((h - 1 - y) * w * bpp, (h - y) * w * bpp), y * w * bpp);
    }
    gl.texImage2D(gl.TEXTURE_2D, 0, internal, w, h, 0, format, gl.UNSIGNED_BYTE, rows);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return t;
  };
  const target = (w, h) => {
    const t = tex(w, h, 'rgba', null);
    const fb = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0);
    return { t, fb, w, h };
  };
  window.__pp = {
    gl,
    progs,
    cam: tex(s.W, s.H, 'rgba', b64(s.photo)),
    mask: tex(s.maskW, s.maskH, 'r', b64(s.mask), false), // preview space: row 0 = top
    lowA: target(s.low.w, s.low.h),
    lowB: target(s.low.w, s.low.h),
    smallA: target(s.small.w, s.small.h),
    smallB: target(s.small.w, s.small.h),
    s,
  };
  return { renderer: gl.getParameter(gl.RENDERER) };
}, setup);
if (init.error) {
  console.error(init.error);
  process.exit(1);
}

const lab = (r, g, b) => {
  const lin = v => {
    v /= 255;
    return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  };
  const R = lin(r);
  const G = lin(g);
  const B = lin(b);
  const f = t => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
  const x = f((0.4124 * R + 0.3576 * G + 0.1805 * B) / 0.95047);
  const y = f(0.2126 * R + 0.7152 * G + 0.0722 * B);
  const z = f((0.0193 * R + 0.1192 * G + 0.9505 * B) / 1.08883);
  return [116 * y - 16, 500 * (x - y), 200 * (y - z)];
};
const faceMask = readPnm(join(work, 'faces.pgm'));

for (const r of recipes) {
  const gpu64 = await page.evaluate(r => {
    const { gl, progs, s } = window.__pp;
    const pp = window.__pp;
    const quad = () => gl.drawArrays(gl.TRIANGLES, 0, 3);
    const use = (p, fb, w, h) => {
      gl.useProgram(p);
      gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
      gl.viewport(0, 0, w, h);
      gl.uniformMatrix4fv(gl.getUniformLocation(p, 'uCamMatrix'), false, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
    };
    const bind = (p, name, unit, t) => {
      gl.activeTexture(gl.TEXTURE0 + unit);
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.uniform1i(gl.getUniformLocation(p, name), unit);
    };
    // Target uv (bottom-left origin) -> preview space (top-left), column-major.
    const orient = [1, 0, 0, 0, -1, 0, 0, 1, 1];
    const orientJ = [1, 0, 0, -1];
    const down = (dst, step) => {
      use(progs.down, dst.fb, dst.w, dst.h);
      bind(progs.down, 'uCamera', 0, pp.cam);
      bind(progs.down, 'uMask', 1, pp.mask);
      gl.uniformMatrix3fv(gl.getUniformLocation(progs.down, 'uOrient'), false, orient);
      gl.uniform2f(gl.getUniformLocation(progs.down, 'uStep'), step[0], step[1]);
      gl.uniform1f(gl.getUniformLocation(progs.down, 'uHasMask'), 1);
      quad();
    };
    const blur = (a, b, wts) => {
      for (const [src, dst, dir] of [
        [a, b, [1 / a.w, 0]],
        [b, a, [0, 1 / a.h]],
      ]) {
        use(progs.blur, dst.fb, dst.w, dst.h);
        bind(progs.blur, 'uSource', 0, src.t);
        gl.uniform2f(gl.getUniformLocation(progs.blur, 'uDir'), dir[0], dir[1]);
        gl.uniform1fv(gl.getUniformLocation(progs.blur, 'uWeights'), wts.weights);
        gl.uniform1i(gl.getUniformLocation(progs.blur, 'uTaps'), wts.taps);
        quad();
      }
    };
    down(pp.lowA, [s.low.f / s.W, s.low.f / s.H]);
    blur(pp.lowA, pp.lowB, s.lowBlur);
    down(pp.smallA, [2 / s.W, 2 / s.H]);
    blur(pp.smallA, pp.smallB, s.smallBlur);
    const p = progs.comp;
    use(p, null, s.W, s.H);
    bind(p, 'uCamera', 0, pp.cam);
    bind(p, 'uLow', 1, pp.lowA.t);
    bind(p, 'uSmall', 2, pp.smallA.t);
    bind(p, 'uMask', 3, pp.mask);
    const U = n => gl.getUniformLocation(p, n);
    gl.uniformMatrix3fv(U('uOrient'), false, orient);
    gl.uniformMatrix2fv(U('uOrientJ'), false, orientJ);
    gl.uniform1f(U('uHasMask'), 1);
    gl.uniform1f(U('uMaskSharpen'), 0);
    gl.uniform2f(U('uLowSize'), s.low.w, s.low.h);
    gl.uniform2f(U('uSize'), s.W, s.H);
    gl.uniform1fv(U('uG'), r.grade);
    gl.uniform1fv(U('uScene'), s.scene);
    gl.uniform1fv(U('uSkin'), r.portrait.slice(1, 5));
    gl.uniform1f(U('uBlur'), r.portrait[0]);
    gl.uniform1fv(U('uFaceP'), r.face);
    const f = s.face;
    gl.uniform1f(U('uFaceOn'), f ? 1 : 0);
    if (f) {
      gl.uniform4fv(U('uFaceFrame'), f.frame);
      gl.uniform2fv(U('uFaceHalf'), f.half);
      gl.uniform3fv(U('uFaceMarks'), f.marks);
      gl.uniform4fv(U('uEyes'), f.eyes);
      gl.uniform1f(U('uEyeW'), f.eyeW);
      gl.uniform3fv(U('uMouth'), f.mouth);
    }
    gl.uniform1f(U('uStudioMean'), s.studioMean);
    quad();
    const px = new Uint8Array(s.W * s.H * 4);
    gl.readPixels(0, 0, s.W, s.H, gl.RGBA, gl.UNSIGNED_BYTE, px);
    let bin = '';
    // Back to top-down rows, RGB.
    const rgb = new Uint8Array(s.W * s.H * 3);
    for (let y = 0; y < s.H; y++) {
      for (let x = 0; x < s.W; x++) {
        const i = ((s.H - 1 - y) * s.W + x) * 4;
        const o = (y * s.W + x) * 3;
        rgb[o] = px[i];
        rgb[o + 1] = px[i + 1];
        rgb[o + 2] = px[i + 2];
      }
    }
    for (let i = 0; i < rgb.length; i += 32768) bin += String.fromCharCode.apply(null, rgb.subarray(i, i + 32768));
    return btoa(bin);
  }, r);
  const gpu = Buffer.from(gpu64, 'base64');
  const cpu = readPnm(join(out, `cpu_${r.id}.ppm`)).data;
  const all = [];
  const skin = [];
  for (let i = 0; i < W * H; i += 3) {
    const a = lab(cpu[i * 3], cpu[i * 3 + 1], cpu[i * 3 + 2]);
    const b = lab(gpu[i * 3], gpu[i * 3 + 1], gpu[i * 3 + 2]);
    const d = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    all.push(d);
    const x = i % W;
    const y = Math.floor(i / W);
    const fx = Math.floor((x * faceMask.w) / W);
    const fy = Math.floor((y * faceMask.h) / H);
    if (faceMask.data[fy * faceMask.w + fx] > 127) skin.push(d);
  }
  const stats = v => {
    const s = [...v].sort((p, q) => p - q);
    return { mean: +(s.reduce((p, q) => p + q, 0) / s.length).toFixed(2), p95: +s[Math.floor(s.length * 0.95)].toFixed(2) };
  };
  results[r.id] = { all: stats(all), face: skin.length ? stats(skin) : null };
  // CPU | GPU side by side.
  const side = Buffer.alloc(W * 2 * H * 3);
  for (let y = 0; y < H; y++) {
    cpu.copy(side, y * W * 6, y * W * 3, (y + 1) * W * 3);
    gpu.copy(side, y * W * 6 + W * 3, y * W * 3, (y + 1) * W * 3);
  }
  writePpm(join(out, `side_${r.id}.ppm`), W * 2, H, side);
}
await browser.close();
writeFileSync(join(out, 'parity.json'), JSON.stringify({ renderer: init.renderer, gradeOnly, results }, null, 1));
console.log(JSON.stringify({ renderer: init.renderer, gradeOnly, results }, null, 1));
