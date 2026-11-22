#version 450
// Reads a texture from the material group (set 2), which lands at texture unit 16 on OpenGL.

layout(set = 2, binding = 0) uniform sampler2D albedo;

layout(location = 0) in vec2 vUv;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(albedo, vUv);
}
