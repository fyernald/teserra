#version 450
// Multi-channel signed distance field glyphs (msdfgen): the median of the three channels is the distance,
// 0.5 on the outline; the screen-space distance range turns it into a pixel-wide antialiased edge.

layout(set = 2, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

// The distance range of the atlas in its pixels (msdfgen Range), as baked.
const float kPixelRange = 4.0;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    const vec3 msd = texture(atlas, vUv).rgb;
    const float distance = median(msd.r, msd.g, msd.b);
    const vec2 unitRange = vec2(kPixelRange) / vec2(textureSize(atlas, 0));
    const vec2 screenTexSize = vec2(1.0) / fwidth(vUv);
    const float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    const float opacity = clamp(screenPxRange * (distance - 0.5) + 0.5, 0.0, 1.0);
    fragColor = vec4(1.0, 1.0, 1.0, opacity);
}
