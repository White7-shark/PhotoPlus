// One triangle covering the target. vUv: 0..1 in the target (GL origin,
// bottom-left); vCam: the camera texture coordinate for it.
layout(location = 0) in vec2 aPos;
uniform mat4 uCamMatrix;
out vec2 vUv;
out vec2 vCam;

void main() {
  vUv = aPos * 0.5 + 0.5;
  vCam = (uCamMatrix * vec4(vUv, 0.0, 1.0)).xy;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
