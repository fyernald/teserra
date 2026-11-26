#version 450
// Dear ImGui's vertices (ImDrawVert): positions in the display's pixels with y down, a texture coordinate and
// an sRGB-encoded color. The frame block maps display pixels to clip space (+Y up, ADR-0024) and says whether
// the target is sRGB, in which case the color is decoded so that the target encodes it back unchanged.

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(set = 0, binding = 0) uniform ImGuiFrame {
    vec2 scale;
    vec2 translate;
    uint decodeSrgb;
} frame;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

void main() {
    vUv = uv;
    vColor = frame.decodeSrgb != 0u ? vec4(srgbToLinear(color.rgb), color.a) : color;
    gl_Position = vec4(position * frame.scale + frame.translate, 0.0, 1.0);
}
