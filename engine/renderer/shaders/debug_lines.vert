#version 450
// The built-in debug_lines material of the debug pass: DebugVertex positions in world space through the
// view's camera, with their linear color.

#include <teserra/camera.glsl>

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = color;
    gl_Position = camera.viewProjection * vec4(position, 1.0);
}
