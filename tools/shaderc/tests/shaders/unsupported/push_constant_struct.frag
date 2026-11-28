#version 450
// std140 pads the 4-byte struct to 16 bytes, which moves the member after it.
struct Scale { float x; };
layout(push_constant) uniform Scaled { Scale scale; float bias; } push;
layout(location = 0) out vec4 color;
void main() { color = vec4(push.scale.x, push.bias, 0.0, 1.0); }
