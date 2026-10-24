#version 450
// One channel from each uniform block: red from set 0, green and blue from bindings 0 and 1 of set 2, and
// alpha from set 3, which moves with a dynamic offset. Each block lands at its own GL binding (0, 16, 17
// and 24), so a wrong remap shows up as a wrong channel.

layout(set = 0, binding = 0) uniform Frame {
    vec4 red;
} frame;

layout(set = 2, binding = 0) uniform Material {
    vec4 green;
} material;

layout(set = 2, binding = 1) uniform MaterialExtra {
    vec4 blue;
} extra;

layout(set = 3, binding = 0) uniform Draw {
    vec4 alpha;
} draw;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = frame.red + material.green + extra.blue + draw.alpha;
}
