#version 450
layout(set = 2, binding = 0) uniform texture2D image;
layout(set = 2, binding = 1) uniform sampler linearSampler;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = texture(sampler2D(image, linearSampler), vec2(0.5)); }
