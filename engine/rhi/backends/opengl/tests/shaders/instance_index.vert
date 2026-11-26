#version 450
// A triangle over the whole target that hands gl_InstanceIndex to the fragment shader (#185): with Vulkan's
// semantics it counts from the draw's first instance.

layout(location = 0) flat out int vInstance;

void main() {
    vInstance = gl_InstanceIndex;
    const vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
