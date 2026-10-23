#version 450
#include "broken.glsl"

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(broken);
}
