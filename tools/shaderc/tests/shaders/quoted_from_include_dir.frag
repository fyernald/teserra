#version 450
// "x" is looked up next to this file first, then in the include directories.
#include "teserra/colors.glsl"

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = kTint;
}
