#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in mat4 instanceTransform;
void main() { gl_Position = instanceTransform * vec4(position, 1.0); }
