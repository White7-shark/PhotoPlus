// Box-averaged downsample of the camera frame (linear light, stored as
// sqrt) with the subject mask in alpha. `uStep`: one target texel in camera
// texture units; four bilinear taps cover a 4x4 box.
in vec2 vUv;
in vec2 vCam;
uniform CAMERA_SAMPLER uCamera;
uniform sampler2D uMask;   // subject mask in preview space (row 0 = top)
uniform mat3 uOrient;      // target uv -> preview space (x right, y down, 0..1)
uniform vec2 uStep;
uniform float uHasMask;
out vec4 outColor;

void main() {
  vec2 q = uStep * 0.25;
  vec3 c = toLinear3(texture(uCamera, vCam + vec2(-q.x, -q.y)).rgb) + toLinear3(texture(uCamera, vCam + vec2(q.x, -q.y)).rgb) +
           toLinear3(texture(uCamera, vCam + vec2(-q.x, q.y)).rgb) + toLinear3(texture(uCamera, vCam + vec2(q.x, q.y)).rgb);
  float a = uHasMask > 0.5 ? texture(uMask, (uOrient * vec3(vUv, 1.0)).xy).r : 1.0;
  outColor = vec4(encodeLow(c * 0.25), a);
}
