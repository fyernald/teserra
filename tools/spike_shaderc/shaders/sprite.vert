#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

// Set 3: per draw, with a dynamic offset.
layout(set = 3, binding = 0, std140) uniform Draw {
    mat4 model;
} draw;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    vUv = uv;
    vColor = color;
    gl_Position = frame.viewProjection * draw.model * vec4(position, 1.0);
}
