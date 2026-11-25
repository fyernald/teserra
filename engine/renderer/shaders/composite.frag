#version 450
// The built-in composite material: the scene target, sampled with the nearest texel so that integer scaling
// keeps pixel art crisp.

layout(set = 2, binding = 0) uniform sampler2D sceneColor;

layout(location = 0) in vec2 vUv;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(sceneColor, vUv);
}
