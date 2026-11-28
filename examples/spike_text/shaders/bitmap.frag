#version 450
// Bitmap glyphs: coverage in the atlas alpha, baked by stb_truetype at one size.

layout(set = 2, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(1.0, 1.0, 1.0, texture(atlas, vUv).a);
}
