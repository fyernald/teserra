#version 450
// The fallback of fragment shader assets that failed to load (renderer::registerAssetLoaders()): opaque
// magenta, with no inputs or bindings, so it fits any vertex shader and pipeline layout.

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(1.0, 0.0, 1.0, 1.0);
}
