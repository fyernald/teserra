#version 450
// No #extension line: teserra_shaderc enables #include itself.
#include "common/frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = color;
    gl_Position = frame.viewProjection * vec4(position, 1.0);
}
