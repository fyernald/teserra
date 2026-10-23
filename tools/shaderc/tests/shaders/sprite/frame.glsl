// Set 0: per frame (docs/architecture/rhi.md#binding-model). 68 bytes declared, 80 in std140 ranges.
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 viewProjection;
    float time;
} frame;
