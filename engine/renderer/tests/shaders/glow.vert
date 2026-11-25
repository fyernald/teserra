#version 450
// A sprite material's vertex shader for the material tests: sprite vertices through the camera, and a
// parameter of the material's block in the vertex stage too.

#include <teserra/camera.glsl>

layout(set = 2, binding = 0, std140) uniform Glow {
    vec4 tint;
    float strength;
    vec2 offset;
} glow;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    vUv = uv;
    vColor = color;
    gl_Position = camera.viewProjection * vec4(position + vec3(glow.offset, 0.0), 1.0);
}
