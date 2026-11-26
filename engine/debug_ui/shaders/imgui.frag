#version 450
// A Dear ImGui triangle: its vertex color times its texture (the font atlas, or an image a window shows).

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;

layout(set = 2, binding = 0) uniform sampler2D image;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor * texture(image, vUv);
}
