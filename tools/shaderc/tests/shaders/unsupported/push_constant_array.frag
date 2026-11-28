#version 450
// std430 packs a float array with 4-byte elements; std140 needs 16 bytes.
layout(push_constant) uniform Weights { float weights[4]; } push;
layout(location = 0) out vec4 color;
void main() { color = vec4(push.weights[1]); }
