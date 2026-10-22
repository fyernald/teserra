#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(undeclared, 1.0);
}
