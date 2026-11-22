#version 450
// Reads the image from the material group (set 2), with the sampler of the bound group: nearest or linear.

layout(set = 2, binding = 0) uniform sampler2D albedo;

layout(location = 0) in vec2 vUv;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(albedo, vUv);
}
