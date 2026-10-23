#version 450

layout(location = 0) out vec4 fragColor;

void main() {
    // The undeclared identifier is on line 7.
    fragColor = vec4(undeclaredColor, 1.0);
}
