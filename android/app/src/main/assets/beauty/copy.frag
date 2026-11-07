// The camera as it is ("Original", or no beauty).
in vec2 vUv;
in vec2 vCam;
uniform CAMERA_SAMPLER uCamera;
out vec4 outColor;

void main() { outColor = vec4(texture(uCamera, vCam).rgb, 1.0); }
