#version 450
// The built-in debug_lines material: the line's color, alpha-blended over the frame.

layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor;
}
