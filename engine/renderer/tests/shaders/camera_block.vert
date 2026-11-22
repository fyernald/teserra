#version 450
// Uses the engine's camera block, so that its reflection can be compared with renderer::CameraData.

#include <teserra/camera.glsl>

layout(location = 0) in vec3 position;

void main() {
    gl_Position = camera.viewProjection * vec4(position, 1.0) + vec4(0.0, 0.0, 0.0, camera.time * 0.0);
}
