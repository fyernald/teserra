#version 450
// The fallback of vertex shader assets that failed to load (renderer::registerAssetLoaders()). It has no
// inputs, so it fits any vertex layout, and puts every vertex at one point, so its triangles are empty and
// draw nothing. The missing file is in the log.

void main() {
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
