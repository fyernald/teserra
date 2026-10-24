#version 450
// Passes each vertex's color on: the shader of the GPU tests' pipelines and triangles.

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = color;
    gl_Position = vec4(position, 1.0);
}
