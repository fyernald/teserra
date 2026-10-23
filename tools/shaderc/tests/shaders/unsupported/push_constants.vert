#version 450
layout(push_constant) uniform Constants { mat4 transform; } constants;
layout(location = 0) in vec3 position;
void main() { gl_Position = constants.transform * vec4(position, 1.0); }
