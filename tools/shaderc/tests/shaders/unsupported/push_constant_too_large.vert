#version 450
// 192 bytes, above the 128 bytes every backend offers.
layout(push_constant) uniform Large { mat4 a; mat4 b; mat4 c; } large;
void main() { gl_Position = large.a * large.b * large.c * vec4(1.0); }
