#version 450
layout(set = 2, binding = 0) uniform sampler2D layers[2];
layout(location = 0) out vec4 fragColor;
void main() { fragColor = texture(layers[0], vec2(0.5)) + texture(layers[1], vec2(0.5)); }
