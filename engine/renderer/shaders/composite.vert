#version 450
// The built-in composite material: one triangle that covers the viewport, with UVs from {0, 0} at its top-left
// corner to {1, 1} at its bottom-right one (ADR-0024: NDC +Y is up, UV rows count from the top).

layout(location = 0) out vec2 vUv;

void main() {
    const vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUv = uv;
    gl_Position = vec4((uv.x * 2.0) - 1.0, 1.0 - (uv.y * 2.0), 0.0, 1.0);
}
