#version 450
#extension GL_GOOGLE_include_directive : require
#include <teserra/colors.glsl>

layout(set = 2, binding = 0) uniform sampler2D albedo;

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(albedo, vec2(0.5)) * vColor * kTint;
}
