/**
 * Uniform preparation for the live beauty shaders (desktop mirror of
 * android/.../beauty/LiveBeautyUniforms.kt; the Kotlin unit test checks the
 * same numbers via android/app/src/test/resources/beauty_uniforms_fixture.json).
 */

export const LOW_SIGMA = 0.015; // fraction of the short side (engine: kLowSigma)
export const SMALL_SIGMA = 0.004; // skin-smoothing base, fraction of the short side

/** Low-resolution target: factor and size, as the engine's grade. */
export function lowSize(w, h) {
  const f = Math.max(1, Math.round(Math.min(w, h) / 256));
  return { f, w: Math.max(1, Math.ceil(w / f)), h: Math.max(1, Math.ceil(h / f)) };
}

/** Normalised Gaussian weights for offsets 0..taps-1 (at most 13). */
export function blurWeights(sigma) {
  const taps = Math.min(13, Math.max(2, Math.ceil(3 * sigma) + 1));
  const w = [];
  for (let i = 0; i < taps; i++) w.push(Math.exp((-0.5 * i * i) / (sigma * sigma)));
  const sum = w[0] + 2 * w.slice(1).reduce((a, b) => a + b, 0);
  return { taps, weights: [...w.map(v => v / sum), ...Array(13 - taps).fill(0)] };
}

const range = (pts, first, n) => pts.slice(first, first + n);
const centre = ps => [ps.reduce((s, p) => s + p[0], 0) / ps.length, ps.reduce((s, p) => s + p[1], 0) / ps.length];

/** Face-frame uniforms from 131 landmarks (normalised x, y pairs) in a w x h image. */
export function faceUniforms(points, w, h) {
  const pts = [];
  for (let i = 0; i < 131; i++) pts.push([points[2 * i] * w, points[2 * i + 1] * h]);
  const oval = range(pts, 0, 36);
  let a = centre(range(pts, 56, 16));
  let b = centre(range(pts, 72, 16));
  if (a[0] > b[0]) [a, b] = [b, a];
  const ed = Math.hypot(b[0] - a[0], b[1] - a[1]);
  const right = [(b[0] - a[0]) / ed, (b[1] - a[1]) / ed];
  const down = [-right[1], right[0]];
  const fc = centre(oval);
  const along = (q, ax) => (q[0] - fc[0]) * ax[0] + (q[1] - fc[1]) * ax[1];
  let halfW = 1;
  let halfH = 1;
  for (const q of oval) {
    halfW = Math.max(halfW, Math.abs(along(q, right)));
    halfH = Math.max(halfH, Math.abs(along(q, down)));
  }
  const width = ps => {
    const v = ps.map(q => along(q, right));
    return Math.max(...v) - Math.min(...v);
  };
  const mouthIn = [...range(pts, 99, 9), ...range(pts, 108, 9)];
  const mouthOut = range(pts, 88, 11);
  return {
    frame: [fc[0], fc[1], right[0], right[1]],
    half: [halfW, halfH],
    marks: [
      along(centre([a, b]), down) / halfH,
      along(centre(range(pts, 128, 3)), down) / halfH,
      along(centre(mouthIn), down) / halfH,
    ],
    eyes: [a[0], a[1], b[0], b[1]],
    eyeW: Math.max(4, 0.5 * (width(range(pts, 56, 16)) + width(range(pts, 72, 16)))),
    mouth: [...centre(mouthOut), Math.max(4, width(mouthOut))],
    faceWidth: Math.max(...oval.map(q => q[0])) - Math.min(...oval.map(q => q[0])),
  };
}

/** Mean studio-light shading over the face (the engine averages it over the oval). */
export function studioMean(lightAngle) {
  const la = Math.max(-1, Math.min(1, lightAngle));
  let L = [0.85 * la, -0.35, 0.6];
  const n = Math.hypot(...L);
  L = L.map(v => v / n);
  let sum = 0;
  let count = 0;
  for (let j = -40; j <= 40; j++) {
    for (let i = -40; i <= 40; i++) {
      const u = i / 40;
      const v = j / 40;
      if (u * u + v * v > 1) continue;
      const su = u / 1.15;
      const sv = v / 1.15;
      const z = Math.sqrt(Math.max(0, 1 - su * su - sv * sv));
      sum += Math.max(0, su * L[0] + sv * L[1] + z * L[2]);
      count++;
    }
  }
  return sum / count;
}
