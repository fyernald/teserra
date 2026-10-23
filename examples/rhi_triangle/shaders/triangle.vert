#version 450
// The vertex-colored triangle of rhi_triangle, compiled by teserra_add_shaders() into a .tshader blob.

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = color;
    gl_Position = vec4(position, 1.0);
}
