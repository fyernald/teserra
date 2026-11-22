// The camera block of every engine shader, at set 0 (rhi::kFrameGroup), binding 0. Its CPU side is
// renderer::CameraData (<teserra/renderer/camera_data.hpp>); the renderer tests check that both have the
// same std140 size. Include it with #include <teserra/camera.glsl>.

#ifndef TESERRA_CAMERA_GLSL
#define TESERRA_CAMERA_GLSL

layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    // The camera's world position in xyz; w is 1.
    vec4 position;
    // The size of the view's viewport in pixels.
    vec2 viewportSize;
    // Seconds of game time.
    float time;
} camera;

#endif
