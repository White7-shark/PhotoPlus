// The live beauty composite, per pixel at preview resolution:
//   A skin   frequency separation against a small blur (smooth, spots, even, lift)
//   B face   blush / contour / highlight (makeupAt, as the engine), studio key
//            light, eyes, lips, under-eye, shine, face light — on the largest face
//   C D E    shadeGrade: the engine's grade, line for line (beauty.cpp shade())
// The saved photo always runs the full engine; the preview matches its look.
in vec2 vUv;
in vec2 vCam;
uniform CAMERA_SAMPLER uCamera;
uniform sampler2D uLow;    // blurred (sigma 1.5% of the short side): sqrt-linear RGB, blurred alpha
uniform sampler2D uSmall;  // small blur for skin: sqrt-linear RGB
uniform sampler2D uMask;   // subject mask in preview space (row 0 = top)
uniform mat3 uOrient;      // target uv -> preview space p (x right, y down, 0..1): what the user sees
uniform mat2 uOrientJ;     // d(target uv) / dp, to turn texture gradients into preview space
uniform float uHasMask;
uniform float uMaskSharpen;  // 1: tighten an upsampled low-resolution mask
uniform vec2 uLowSize;       // low texture size in texels
uniform vec2 uSize;          // preview size in pixels (upright)
uniform float uG[58];        // pp_beauty_grade, in order
uniform float uScene[9];     // hasSkin, skinY, skinCb, skinCr, skinSpread, subjectX, subjectW, lightAngle, skinYSpread
uniform float uSkin[4];      // smooth, blemish, even, brighten
uniform float uBlur;         // Portrait depth blur 0..1 (preview: the background softened; photos get the lens blur)
uniform float uFaceP[13];    // eyes, teeth, lips, underEye, shine, faceLight, studio, blush, blushRGB, contour, highlight
uniform float uFaceOn;
uniform vec4 uFaceFrame;     // centre (preview px), `right` unit vector
uniform vec2 uFaceHalf;      // half width, half height (px)
uniform vec3 uFaceMarks;     // eye, nose, mouth lines (face units, v)
uniform vec4 uEyes;          // left eye, right eye centres (px)
uniform float uEyeW;         // eye width (px)
uniform vec3 uMouth;         // mouth centre (px), width (px)
uniform float uStudioMean;   // the studio shading's mean over the face (see shadeMeanFor)
out vec4 outColor;

const float PI = 3.14159265;

float gainOf(float v, float g) { return g > 1.0 ? v * g / (1.0 + v * (g - 1.0)) : v * g; }
vec3 gainOf3(vec3 c, float g) { return vec3(gainOf(c.r, g), gainOf(c.g, g), gainOf(c.b, g)); }
vec3 colorAt(int i) { return vec3(uG[i], uG[i + 1], uG[i + 2]); }
vec3 unitLight(vec3 c) {
  vec3 l = toLinear3(c);
  return l / max(lum(l), 1e-3);
}
vec2 chromaDir(vec3 c) {
  vec3 y = rgbToYcc(c);
  float n = length(y.yz);
  return n > 1e-4 ? y.yz / n : vec2(0.0);
}
float rollOff(float v, float original, float knee, float ceiling) {
  if (original >= ceiling) return min(v, original);
  float hi = max(original, knee);
  if (v <= hi) return v;
  float t = (v - hi) / (ceiling - hi);
  float e = exp(-2.0 * t);
  return hi + (ceiling - hi) * (1.0 - e) / (1.0 + e);  // tanh
}

float skinWeight(vec3 rgb, float a) {
  if (uScene[0] < 0.5) return 0.0;
  vec3 y = rgbToYcc(rgb);
  float k = 1.0 / (y.x + 0.05);
  float yk = uScene[1] + 0.05;
  float sigma = max(0.1, 2.5 * uScene[4]);
  float dc = length(y.yz * k - vec2(uScene[2], uScene[3]) / yk) / sigma;
  return a * exp(-0.5 * dc * dc) * smoothstep(0.04, 0.12, y.x) * (1.0 - smoothstep(0.96, 1.0, y.x));
}

// Mirrors photoplus::portrait::makeupAt.
float bump(float u, float v, float cu, float cv, float ru, float rv) {
  float du = (u - cu) / ru, dv = (v - cv) / rv;
  return exp(-0.5 * (du * du + dv * dv));
}
vec3 makeupAt(float u, float v) {
  float eyeV = uFaceMarks.x, noseV = uFaceMarks.y, mouthV = uFaceMarks.z;
  float au = abs(u);
  float appleV = eyeV + 0.55 * (noseV - eyeV) + 0.08;
  float blush = bump(au, v, 0.48, appleV, 0.2, 0.15);
  float hollowV = noseV + 0.08;
  float contour = min(1.0, bump(au, v + 0.35 * (au - 0.7), 0.7, hollowV, 0.2, 0.09) + 0.7 * bump(au, v, 0.82, eyeV - 0.42, 0.14, 0.22) +
                               0.8 * smoothstep(0.62, 0.92, au) * smoothstep(eyeV + 0.1, noseV + 0.1, v));
  float highlight = min(1.0, bump(u, v, 0.0, 0.5 * (eyeV + noseV), 0.07, 0.28) + 0.8 * bump(u, v, 0.0, eyeV - 0.5, 0.24, 0.15) +
                                 0.75 * bump(au, v, 0.46, eyeV + 0.24, 0.17, 0.07) + 0.6 * bump(u, v, 0.0, 0.86, 0.12, 0.07));
  return vec3(blush, contour, highlight);
}

// ---- C, D, E: mirrors beauty.cpp shade() --------------------------------------
vec3 shadeGrade(vec3 rgb, vec2 p, float a, vec3 low, float lowA, vec2 grad, float s) {
  float temperature = uG[0], tint = uG[1], contrast = uG[2], blacks = uG[3], highlights = uG[4], saturation = uG[5];
  float vibrance = uG[6];
  float shadowAmount = uG[10], highlightAmount = uG[14];
  float bandWidth = uG[18] * PI / 180.0, bandShift = uG[19] * PI / 180.0, bandSat = uG[20];
  float skinProtect = uG[21], skinWarmth = uG[22], skinRosy = uG[23], skinLuminance = uG[24];
  float subjectLight = uG[25], lightWarmth = uG[26], shadowLift = uG[27], rim = uG[28];
  float bloom = uG[32], softFocus = uG[33], skinSheen = uG[34];
  float bgExposure = uG[35], bgSaturation = uG[36], bgTintAmount = uG[40];
  float bgGlow = uG[41], bgGlowX = uG[42], bgGlowY = uG[43], bgGlowSize = uG[44];
  float bgVignette = uG[48], bgHaze = uG[49], bgWarmth = uG[50];
  float lightAngle = uScene[7];
  float aspect = uSize.x / uSize.y;

  vec3 orig = toLinear3(rgb);
  vec3 lin = orig;
  float wbg = 1.0 - a;
  // D: lighting
  if (softFocus > 0.0) {
    lin = mix(lin, low, 0.35 * softFocus);
    lin = lin + low * (vec3(1.0) - lin) * (0.3 * softFocus);
  }
  float lowY = lum(low);
  if (bloom > 0.0) {
    float h = max(lowY - 0.45, 0.0) / 0.55;
    lin = lin + low * (vec3(1.0) - lin) * (bloom * 0.6 * h * h);
  }
  if (shadowLift > 0.0 && a > 0.0) {
    float dark = 1.0 - smoothstep(0.02, 0.22, lowY);
    float pxDark = 1.0 - smoothstep(0.03, 0.35, lum(lin));
    lin = lin * (1.0 + shadowLift * 1.5 * dark * pxDark * a);
  }
  if (subjectLight != 0.0 && a > 0.0) {
    float side = clamp((p.x - uScene[5]) / (0.5 * uScene[6] + 1e-3), -1.5, 1.5) * lightAngle;
    float gs = exp2(subjectLight * 0.5 * (1.0 + 0.3 * side));
    lin = gainOf3(lin, mix(1.0, gs, a));
  }
  if (lightWarmth != 0.0 && a > 0.0) {
    vec3 key = lightWarmth >= 0.0 ? vec3(1.1, 1.0, 0.82) : vec3(0.9, 1.0, 1.14);
    key /= lum(key);
    lin = lin * mix(vec3(1.0), key, a * 0.6 * abs(lightWarmth) * (1.0 - 0.7 * s * skinProtect));
  }
  if (rim > 0.0 && a > 0.0) {
    float band = a * smoothstep(0.15, 0.7, 1.0 - lowA);
    float dirW = 0.35;
    float gl = length(grad);
    vec2 rimDir = normalize(vec2(-lightAngle, -0.6));
    if (gl > 1e-3) dirW = clamp(0.35 + 0.65 * dot(-grad, rimDir) / gl, 0.0, 1.0);
    float sp = s * skinProtect;
    lin = lin + mix(unitLight(colorAt(29)), vec3(1.0), sp) * (rim * 0.35 * band * dirW * (1.0 - 0.5 * sp));
  }
  // C: background
  if (wbg > 0.0) {
    if (bgExposure != 0.0) lin = mix(lin, gainOf3(lin, exp2(bgExposure)), wbg);
    if (bgSaturation != 0.0) {
      float Y = lum(lin);
      float k = 1.0 + bgSaturation * (bgSaturation > 0.0 ? 0.6 : 1.0);
      vec3 c = max(vec3(Y) + (lin - vec3(Y)) * k, vec3(0.0));
      lin = mix(lin, c, wbg);
    }
    if (bgTintAmount > 0.0) lin = mix(lin, unitLight(colorAt(37)) * lum(lin), bgTintAmount * 0.7 * wbg);
    if (bgWarmth != 0.0) {
      vec3 bw = bgWarmth >= 0.0 ? vec3(1.16, 1.0, 0.78) : vec3(0.86, 1.0, 1.2);
      lin = lin * mix(vec3(1.0), bw / lum(bw), wbg * abs(bgWarmth));
    }
  }
  float diag = sqrt(aspect * aspect + 1.0);
  vec3 glowLin = toLinear3(colorAt(45));
  if (bgGlow > 0.0) {
    vec2 d2 = vec2((p.x - bgGlowX) * aspect, p.y - bgGlowY);
    float d = length(d2) / (max(bgGlowSize, 0.02) * diag);
    lin = lin + glowLin * (bgGlow * 0.55 * exp(-0.5 * d * d) * (wbg + 0.2 * a));
  }
  if (bgHaze > 0.0) lin = lin + (glowLin * 0.7 - lin) * (bgHaze * 0.22 * wbg);
  if (bgVignette > 0.0) {
    float vd = length(vec2((p.x - 0.5) * aspect, p.y - 0.5)) / (0.5 * diag);
    lin = lin * (1.0 - bgVignette * 0.6 * smoothstep(0.45, 1.05, vd) * (0.25 + 0.75 * wbg));
  }
  // E: white balance, kept off skin
  vec3 wb = vec3(1.0 + 0.18 * temperature, 1.0 - 0.12 * tint, 1.0 - 0.22 * temperature);
  wb /= max(lum(wb), 1e-3);
  lin = mix(lin * wb, lin, s * skinProtect);
  lin = vec3(rollOff(lin.r, orig.r, 0.8, 0.99), rollOff(lin.g, orig.g, 0.8, 0.99), rollOff(lin.b, orig.b, 0.8, 0.99));
  // E: tone and colour
  vec3 yc = rgbToYcc(toGamma3(lin));
  float y = yc.x, cb = yc.y, cr = yc.z;
  float y0 = y;
  if (contrast != 0.0) {
    float d = y - 0.5;
    y += 0.45 * contrast * d * (1.0 - 4.0 * d * d);
  }
  if (blacks > 0.0) {
    y = blacks * 0.07 + y * (1.0 - blacks * 0.07);
  } else if (blacks < 0.0) {
    float k = -blacks * 0.05;
    y = max(0.0, (y - k) / (1.0 - k));
  }
  if (highlights > 0.0) y += highlights * 0.18 * smoothstep(0.45, 1.0, y) * (1.0 - y);
  else if (highlights < 0.0) y -= -highlights * 0.25 * smoothstep(0.6, 1.0, y) * (y - 0.6);
  if (skinLuminance > 0.0) {
    float k = skinLuminance * 0.3 * s;
    y = y * (1.0 + k) / (1.0 + k * y);
  } else if (skinLuminance < 0.0) {
    y *= 1.0 + 0.15 * skinLuminance * s;
  }
  if (skinSheen > 0.0) y += skinSheen * 0.35 * s * max(0.0, y - uScene[1]) * (1.0 - y);
  y = clamp(y, 0.0, 1.0);
  float kk = clamp((y + 0.02) / (y0 + 0.02), 0.0, 1.6);
  cb *= kk;
  cr *= kk;
  float sat = length(vec2(cb, cr));
  float sf = (1.0 + saturation * 0.5) * (1.0 + vibrance * 0.7 * (1.0 - smoothstep(0.0, 0.22, sat)));
  sf = mix(sf, 1.0 + (sf - 1.0) * 0.4, s);
  cb *= sf;
  cr *= sf;
  float prot = 1.0 - s * skinProtect;
  if (bandWidth > 0.0) {
    vec3 bc = rgbToYcc(colorAt(15));
    float dh = atan(cr, cb) - atan(bc.z, bc.y);
    dh = dh - 2.0 * PI * floor(dh / (2.0 * PI) + 0.5);
    float w = (1.0 - smoothstep(0.5 * bandWidth, bandWidth, abs(dh))) * smoothstep(0.02, 0.06, sat) * prot;
    if (w > 0.0) {
      float ang = bandShift * w, c = cos(ang), sn = sin(ang), m = 1.0 + bandSat * w;
      vec2 n = vec2(cb * c - cr * sn, cb * sn + cr * c) * m;
      cb = n.x;
      cr = n.y;
    }
  }
  float wsh = 1.0 - smoothstep(0.08, 0.5, y), whl = smoothstep(0.5, 0.95, y);
  vec2 sh = shadowAmount * 0.06 * chromaDir(colorAt(7));
  vec2 hl = highlightAmount * 0.06 * chromaDir(colorAt(11));
  cb += (sh.x * wsh + hl.x * whl) * prot;
  cr += (sh.y * wsh + hl.y * whl) * prot;
  cb += s * (-0.014 * skinWarmth + 0.002 * skinRosy);
  cr += s * (0.008 * skinWarmth + 0.018 * skinRosy);
  vec3 o = yccToRgb(vec3(y, cb, cr));
  return max(vec3(rollOff(o.r, rgb.r, 0.97, 0.995), rollOff(o.g, rgb.g, 0.97, 0.995), rollOff(o.b, rgb.b, 0.97, 0.995)), vec3(0.0));
}

void main() {
  vec3 rgb = texture(uCamera, vCam).rgb;
  vec2 p = (uOrient * vec3(vUv, 1.0)).xy;  // preview space: top-left origin, as the engine
  vec2 px = p * uSize;
  float a = 1.0;
  if (uHasMask > 0.5) {
    a = texture(uMask, p).r;
    if (uMaskSharpen > 0.5) a = smoothstep(0.2, 0.8, a);
  }
  if (uFaceOn > 0.5) {
    // The face is always subject (segmentation can miss a lit cheek).
    vec2 fr = uFaceFrame.zw;
    vec2 fd = px - uFaceFrame.xy;
    float fu = dot(fd, fr) / uFaceHalf.x, fv = dot(fd, vec2(-fr.y, fr.x)) / uFaceHalf.y;
    a = max(a, 1.0 - smoothstep(0.85, 1.0, sqrt(fu * fu + fv * fv)));
  }
  vec4 lowE = texture(uLow, vUv);
  vec3 low = decodeLow(lowE.rgb);
  vec2 lt = 1.0 / uLowSize;
  // Gradient of the blurred alpha per unit of target uv, then in preview space.
  vec2 gUv = vec2((texture(uLow, vUv + vec2(lt.x, 0.0)).a - texture(uLow, vUv - vec2(lt.x, 0.0)).a) * 0.5 * uLowSize.x,
                  (texture(uLow, vUv + vec2(0.0, lt.y)).a - texture(uLow, vUv - vec2(0.0, lt.y)).a) * 0.5 * uLowSize.y);
  vec2 grad = transpose(uOrientJ) * gUv;
  float skinY = uScene[1];
  // ---- A: skin -------------------------------------------------------------
  float s0 = skinWeight(rgb, a);
  if (s0 > 0.0 && (uSkin[0] + uSkin[1] + uSkin[2] + uSkin[3]) > 0.0) {
    vec3 yc = rgbToYcc(rgb);
    vec3 yb = rgbToYcc(toGamma3(decodeLow(texture(uSmall, vUv).rgb)));
    float fine = yc.x - yb.x;
    float soften = 1.0 - smoothstep(0.12 * skinY, 0.3 * skinY, abs(fine));
    yc.x -= s0 * uSkin[0] * 0.8 * fine * soften;
    float spot = smoothstep(0.015, 0.05, -fine) * (1.0 - smoothstep(0.15, 0.3, -fine));
    yc.x += s0 * uSkin[1] * 0.8 * spot * (-fine);
    // Even: chroma toward the wide-area tone, a third of the way to the
    // person's own median (as the engine's retouch), at this pixel's light.
    vec3 yl = rgbToYcc(toGamma3(low));
    float rel = clamp((yc.x + 0.02) / (yl.x + 0.02), 0.0, 1.5);
    vec2 target = rel * (yl.yz + 0.3 * (vec2(uScene[2], uScene[3]) - yl.yz));
    yc.yz += 0.65 * uSkin[2] * s0 * (target - yc.yz);
    // Lift: on skin (exposure-style), and the subject's gentle lift.
    yc.x = min(1.0, yc.x * (1.0 + s0 * 0.07 * uSkin[3] * (1.0 - yc.x) * smoothstep(0.03, 0.3, yc.x)));
    yc.x = min(1.0, yc.x * (1.0 + uSkin[3] * 0.045 * a * (1.0 - yc.x) * smoothstep(0.02, 0.25, yc.x)));
    rgb = clamp(yccToRgb(yc), 0.0, 1.0);
  }
  // ---- B: face (largest face) ----------------------------------------------
  if (uFaceOn > 0.5) {
    vec2 right = uFaceFrame.zw, down = vec2(-right.y, right.x);
    vec2 d = px - uFaceFrame.xy;
    float u = dot(d, right) / uFaceHalf.x, v = dot(d, down) / uFaceHalf.y;
    float r2 = u * u + v * v;
    float oval = 1.0 - smoothstep(0.8, 1.15, sqrt(r2));
    float eyeL = length((px - uEyes.xy) / vec2(0.55 * uEyeW, 0.3 * uEyeW));
    float eyeR = length((px - uEyes.zw) / vec2(0.55 * uEyeW, 0.3 * uEyeW));
    float eyeM = 1.0 - smoothstep(0.7, 1.0, min(eyeL, eyeR));
    float mouth = length((px - uMouth.xy) / vec2(0.55 * uMouth.z, 0.3 * uMouth.z));
    float lipM = 1.0 - smoothstep(0.75, 1.05, mouth);
    float feat = max(eyeM, lipM);
    float onSkin = oval * (1.0 - feat);
    vec3 yc = rgbToYcc(rgb);
    float y0 = yc.x;
    vec3 mk = makeupAt(u, v);
    if (uFaceP[7] > 0.0) {
      vec3 bc = rgbToYcc(vec3(uFaceP[8], uFaceP[9], uFaceP[10]));
      vec2 dir = length(bc.yz) > 1e-4 ? normalize(bc.yz) : vec2(0.0);
      float k = uFaceP[7] * mk.x * onSkin;
      yc.yz += k * 0.055 * dir * (0.6 + 0.6 * skinY);
      yc.x -= k * 0.02 * skinY;
    }
    // Eyes: whites a touch brighter and less red.
    if (uFaceP[0] > 0.0) {
      float sat = length(yc.yz);
      float white = smoothstep(skinY, skinY + 0.25, yc.x) * (1.0 - smoothstep(0.06, 0.13, sat));
      float w = eyeM * uFaceP[0];
      yc.x += w * white * 0.1 * (1.0 - yc.x);
      yc.z -= w * white * 0.6 * max(0.0, yc.z);
    }
    // Lips: richer colour.
    if (uFaceP[2] > 0.0) {
      float sat = length(yc.yz);
      yc.yz *= 1.0 + 0.5 * lipM * uFaceP[2] * (1.0 - smoothstep(0.2, 0.34, sat));
    }
    // Under-eye: lifted toward the skin's own light.
    if (uFaceP[3] > 0.0) {
      float ue = max(1.0 - smoothstep(0.6, 1.0, length((px - uEyes.xy - down * 0.42 * uEyeW) / vec2(0.55 * uEyeW, 0.24 * uEyeW))),
                     1.0 - smoothstep(0.6, 1.0, length((px - uEyes.zw - down * 0.42 * uEyeW) / vec2(0.55 * uEyeW, 0.24 * uEyeW))));
      yc.x += uFaceP[3] * ue * (1.0 - eyeM) * 0.45 * max(0.0, skinY - yc.x);
    }
    // Shine: highlights beyond the skin's own spread tamed on the smooth
    // base (texture stays), their lost colour restored toward the skin's own.
    if (uFaceP[4] > 0.0) {
      float base = rgbToYcc(toGamma3(decodeLow(texture(uSmall, vUv).rgb))).x;
      float over = max(0.0, base - (skinY + 1.6 * uScene[8]));
      if (over > 0.0) {
        float cut = uFaceP[4] * 0.75 * over * onSkin;
        yc.x -= cut;
        float rel = clamp((base - cut + 0.02) / (skinY + 0.02), 0.0, 1.5);
        float k = uFaceP[4] * onSkin * min(1.0, over / 0.05) * 0.6;
        yc.yz += k * (vec2(uScene[2], uScene[3]) * rel - yc.yz);
      }
    }
    // Face light: shadows opened toward the face's own median.
    if (uFaceP[5] > 0.0) yc.x += uFaceP[5] * 0.65 * oval * (1.0 - 0.6 * feat) * max(0.0, skinY - lum(toGamma3(low)));
    float ratio = clamp((yc.x + 0.02) / (y0 + 0.02), 0.5, 1.6);
    yc.yz *= ratio;
    rgb = clamp(yccToRgb(yc), 0.0, 1.0);
    // Linear-light gains: contour, highlight, studio key light.
    float gn = 1.0;
    if (uFaceP[11] > 0.0) gn *= 1.0 - uFaceP[11] * 0.2 * mk.y * oval;
    if (uFaceP[12] > 0.0) gn *= 1.0 + uFaceP[12] * 0.28 * mk.z * onSkin;
    if (uFaceP[6] > 0.0) {
      vec3 L = normalize(vec3(0.85 * clamp(uScene[7], -1.0, 1.0), -0.35, 0.6));
      float su = u / 1.15, sv = v / 1.15;
      float z = sqrt(max(0.0, 1.0 - su * su - sv * sv));
      float shade = max(0.0, su * L.x + sv * L.y + z * L.z);
      gn *= 1.0 + uFaceP[6] * oval * (0.55 * (shade - uStudioMean) + 0.05);
    }
    if (abs(gn - 1.0) > 1e-4) rgb = toGamma3(gainOf3(toLinear3(rgb), gn));
  }
  // Portrait mode's depth blur, previewed: the background softened toward
  // the low-pass copy (the saved photo gets the engine's lens blur).
  if (uBlur > 0.0) rgb = mix(rgb, toGamma3(low), (1.0 - a) * clamp(uBlur * 1.6, 0.0, 1.0));
  float s = skinWeight(rgb, a);
  outColor = vec4(shadeGrade(rgb, p, a, low, lowE.a, grad, s), 1.0);
}
