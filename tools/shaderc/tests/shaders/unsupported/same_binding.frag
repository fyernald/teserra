#version 450
layout(set = 2, binding = 0) uniform sampler2D first;
layout(set = 2, binding = 0) uniform sampler2D second;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = texture(first, vec2(0.5)) + texture(second, vec2(0.5)); }
