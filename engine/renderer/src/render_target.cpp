#include <teserra/renderer/render_target.hpp>

#include <algorithm>
#include <cmath>

namespace teserra::renderer {

namespace {

Vec2 floorOf(Vec2 v) noexcept {
    return {std::floor(v.x), std::floor(v.y)};
}

} // namespace

UVec2 sceneTargetSize(const SceneTargetConfig& config, UVec2 outputSize) noexcept {
    return config.resolution.x != 0 && config.resolution.y != 0 ? config.resolution : outputSize;
}

CompositeRect compositeRect(UVec2 sceneSize, UVec2 outputSize) noexcept {
    if (sceneSize.x == 0 || sceneSize.y == 0 || outputSize.x == 0 || outputSize.y == 0) {
        return {.position = Vec2{0.0F}, .size = Vec2{0.0F}, .scale = 0.0F};
    }
    const u32 factor = std::min(outputSize.x / sceneSize.x, outputSize.y / sceneSize.y);
    const Vec2 scene{static_cast<f32>(sceneSize.x), static_cast<f32>(sceneSize.y)};
    const Vec2 output{static_cast<f32>(outputSize.x), static_cast<f32>(outputSize.y)};
    // Smaller than the scene: no integer factor fits, so it is scaled down to fit instead.
    const f32 scale =
        factor >= 1 ? static_cast<f32>(factor) : std::min(output.x / scene.x, output.y / scene.y);
    const Vec2 size = factor >= 1 ? scene * scale : floorOf(scene * scale);
    // Whole pixels, so every scene pixel covers the same number of output pixels.
    const Vec2 position = floorOf((output - size) * 0.5F);
    return {.position = position, .size = size, .scale = scale};
}

Vec2 outputToScene(const CompositeRect& rect, Vec2 outputPixel) noexcept {
    if (rect.scale <= 0.0F) {
        return Vec2{0.0F};
    }
    return (outputPixel - rect.position) / rect.scale;
}

} // namespace teserra::renderer
