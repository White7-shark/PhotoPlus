// Separable Gaussian on a low-resolution texture (colour in linear light,
// alpha as is). `uDir`: one texel along the pass; weights for offsets 0..12.
in vec2 vUv;
uniform sampler2D uSource;
uniform vec2 uDir;
uniform float uWeights[13];
uniform int uTaps;
out vec4 outColor;

void main() {
  vec4 e = texture(uSource, vUv);
  vec3 c = decodeLow(e.rgb) * uWeights[0];
  float a = e.a * uWeights[0];
  for (int i = 1; i < 13; ++i) {
    if (i >= uTaps) break;
    vec4 p = texture(uSource, vUv + uDir * float(i));
    vec4 m = texture(uSource, vUv - uDir * float(i));
    c += (decodeLow(p.rgb) + decodeLow(m.rgb)) * uWeights[i];
    a += (p.a + m.a) * uWeights[i];
  }
  outColor = vec4(encodeLow(c), a);
}
