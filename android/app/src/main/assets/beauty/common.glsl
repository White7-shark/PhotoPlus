// Shared by the live beauty passes (GLSL ES 3.00). Mirrors the C++ engine
// (native/engine/src/beauty/beauty.cpp and portrait/common.hpp): keep the
// two in step — scripts/beauty-gpu-parity.mjs compares them on real photos.
precision highp float;

float toLinear(float v) {
  v = clamp(v, 0.0, 1.0);
  return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
float toGamma(float v) {
  v = max(v, 0.0);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}
vec3 toLinear3(vec3 c) { return vec3(toLinear(c.r), toLinear(c.g), toLinear(c.b)); }
vec3 toGamma3(vec3 c) { return vec3(toGamma(c.r), toGamma(c.g), toGamma(c.b)); }
float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 rgbToYcc(vec3 c) {
  return vec3(0.299 * c.r + 0.587 * c.g + 0.114 * c.b, -0.168736 * c.r - 0.331264 * c.g + 0.5 * c.b,
              0.5 * c.r - 0.418688 * c.g - 0.081312 * c.b);
}
vec3 yccToRgb(vec3 y) {
  return vec3(y.x + 1.402 * y.z, y.x - 0.344136 * y.y - 0.714136 * y.z, y.x + 1.772 * y.y);
}

// Low-resolution textures hold sqrt(linear) in 8 bits (fine shadows, any GPU).
vec3 decodeLow(vec3 e) { return e * e; }
vec3 encodeLow(vec3 l) { return sqrt(max(l, vec3(0.0))); }
