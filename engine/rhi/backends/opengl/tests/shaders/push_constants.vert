#version 450
// A triangle over the whole target whose color comes from push constants (#135): this stage passes `color`
// on, and the fragment shader multiplies it by `tint`, so both stages read the block.

layout(push_constant) uniform Draw {
    vec4 color;
    vec4 tint;
} draw;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = draw.color;
    const vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
