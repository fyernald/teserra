#version 450
// The quad of textured_quad, compiled by teserra_add_shaders() into a .tshader blob. Positions are in
// normalized device coordinates (+Y up) and fill the viewport, which keeps the quad square; UV (0, 0) is the
// top-left corner of the image (ADR-0024).

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 vUv;

void main() {
    vUv = uv;
    gl_Position = vec4(position, 0.0, 1.0);
}
