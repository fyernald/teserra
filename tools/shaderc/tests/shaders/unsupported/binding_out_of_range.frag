#version 450
layout(set = 1, binding = 8) uniform sampler2D overflow;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = texture(overflow, vec2(0.5)); }
