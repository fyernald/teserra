#version 450
// A triangle that covers the whole target, with UV (0, 0) at its top-left corner (ADR-0024): NDC +Y is up,
// texture row 0 is the top.

layout(location = 0) out vec2 vUv;

void main() {
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 position = positions[gl_VertexIndex];
    vUv = vec2(position.x + 1.0, 1.0 - position.y) * 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
