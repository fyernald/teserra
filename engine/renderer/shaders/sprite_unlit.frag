#version 450
// The built-in sprite_unlit material: the sprite's texture, from the material group (set 2), times its tint.
// Blending is pipeline state (alpha, premultiplied or additive); for premultiplied sprites the texture and
// the tint are premultiplied.

layout(set = 2, binding = 0) uniform sampler2D albedo;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(albedo, vUv) * vColor;
}
