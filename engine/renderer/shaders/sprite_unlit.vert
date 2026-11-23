#version 450
// The built-in sprite_unlit material of the sprite pass: SpriteVertex positions in world space through the
// view's camera, the UV and the linear tint passed on.

#include <teserra/camera.glsl>

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    vUv = uv;
    vColor = color;
    gl_Position = camera.viewProjection * vec4(position, 1.0);
}
