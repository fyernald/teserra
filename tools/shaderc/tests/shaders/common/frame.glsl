// Set 0: per frame. Includes a file next to itself.
#include "constants.glsl"

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 viewProjection;
    float time;
} frame;
