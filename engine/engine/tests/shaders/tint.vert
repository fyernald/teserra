#version 450
// A sprite material for the GPU test: sprite vertices through the camera.

#include <teserra/camera.glsl>

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vUv;

void main() {
    vUv = uv;
    gl_Position = camera.viewProjection * vec4(position, 1.0);
}
