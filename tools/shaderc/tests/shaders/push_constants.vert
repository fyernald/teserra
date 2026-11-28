#version 450
// Push constants (#135): a per-draw transform and color, a std140 uniform block at kPushConstantBinding on
// OpenGL.
layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 color;
} constants;
layout(location = 0) in vec3 position;
layout(location = 0) out vec4 vColor;
void main() {
    vColor = constants.color;
    gl_Position = constants.transform * vec4(position, 1.0);
}
