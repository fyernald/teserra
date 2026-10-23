#version 450
layout(set = 4, binding = 0) uniform Extra { vec4 value; } extra;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = extra.value; }
