#version 450
// A sprite material for the GPU test: the sprite's texture (set 3) times the material's tint (set 2).

layout(set = 2, binding = 0, std140) uniform Tint {
    vec4 color;
} tint;
layout(set = 3, binding = 0) uniform sampler2D spriteTexture;

layout(location = 0) in vec2 vUv;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(spriteTexture, vUv) * tint.color;
}
