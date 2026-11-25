#version 450
// A sprite material's fragment shader for the material tests: the sprite's texture (set 3) times the tint
// of the material's block (set 2), plus a texture of the material.

layout(set = 2, binding = 0, std140) uniform Glow {
    vec4 tint;
    float strength;
    vec2 offset;
} glow;
layout(set = 2, binding = 1) uniform sampler2D noise;
layout(set = 3, binding = 0) uniform sampler2D spriteTexture;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(spriteTexture, vUv) * vColor * glow.tint * glow.strength + texture(noise, vUv) * 0.0;
}
