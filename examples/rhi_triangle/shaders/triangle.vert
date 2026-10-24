#version 450
// The vertex-colored triangle of rhi_triangle, compiled by teserra_add_shaders() into a .tshader blob. It
// turns around the center of the window by the angle in the frame's uniform buffer.

layout(set = 0, binding = 0) uniform Frame {
    // The cosine and sine of the angle.
    vec2 rotation;
    // Scales x and y so that the triangle keeps its shape in windows that are not square.
    vec2 scale;
} frame;

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

void main() {
    const vec2 r = frame.rotation;
    const vec2 turned = vec2(r.x * position.x - r.y * position.y, r.y * position.x + r.x * position.y);
    vColor = color;
    gl_Position = vec4(turned * frame.scale, position.z, 1.0);
}
