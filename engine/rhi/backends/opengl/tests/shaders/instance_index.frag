#version 450
// Writes the instance index into red, one unit per index of an Rgba8Unorm target.

layout(location = 0) flat in int vInstance;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(float(vInstance) / 255.0, 0.0, 0.0, 1.0);
}
