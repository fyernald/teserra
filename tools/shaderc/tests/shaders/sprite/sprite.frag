#version 450
#include "frame.glsl"

// Set 2: per material.
layout(set = 2, binding = 1) uniform sampler2D albedo;
layout(set = 2, binding = 0, std140) uniform Material {
    vec4 tint;
} material;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(albedo, vUv) * vColor * material.tint * (0.5 + 0.5 * sin(frame.time));
}
