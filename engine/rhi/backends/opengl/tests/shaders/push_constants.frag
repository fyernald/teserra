#version 450
// The push-constant color of push_constants.vert, multiplied by the tint.

layout(push_constant) uniform Draw {
    vec4 color;
    vec4 tint;
} draw;

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor * draw.tint;
}
