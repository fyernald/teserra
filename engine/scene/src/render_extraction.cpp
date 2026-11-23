#include <teserra/scene/render_extraction.hpp>

#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include <array>
#include <limits>

namespace teserra::scene {

namespace {

// A box that the first point it grows by replaces: min at +max, max at -max.
Aabb emptyBox() noexcept {
    constexpr f32 kMax = std::numeric_limits<f32>::max();
    return {.min = Vec3{kMax}, .max = Vec3{-kMax}};
}

void grow(Aabb& box, const Vec3& point) noexcept {
    box = math::merge(box, Aabb{.min = point, .max = point});
}

} // namespace

Aabb viewBounds(const renderer::CameraData& camera) noexcept {
    Aabb box = emptyBox();
    // The corners of clip space: x and y in [-1, 1], depth in [0, 1] (ADR-0024).
    for (const f32 x : {-1.0F, 1.0F}) {
        for (const f32 y : {-1.0F, 1.0F}) {
            for (const f32 z : {0.0F, 1.0F}) {
                const Vec4 corner = camera.inverseViewProjection * Vec4{x, y, z, 1.0F};
                grow(box, Vec3{corner} / corner.w);
            }
        }
    }
    return box;
}

Aabb spriteBounds(const SpriteRenderer& sprite, const Mat4& world) noexcept {
    // The quad's corners as the sprite batcher places them: x grows right and y up from the pivot, which is
    // normalized from the image's top-left corner.
    const f32 left = -sprite.pivot.x * sprite.size.x;
    const f32 right = (1.0F - sprite.pivot.x) * sprite.size.x;
    const f32 top = sprite.pivot.y * sprite.size.y;
    const f32 bottom = (sprite.pivot.y - 1.0F) * sprite.size.y;
    Aabb box = emptyBox();
    for (const Vec2 corner :
         std::array{Vec2{left, top}, Vec2{left, bottom}, Vec2{right, bottom}, Vec2{right, top}}) {
        grow(box, Vec3{world * Vec4{corner, 0.0F, 1.0F}});
    }
    return box;
}

void renderExtractionSystem(ecs::SystemContext& context) {
    RenderExtraction& extraction = context.resource<RenderExtraction>();
    extraction.packet.clear();
    extraction.viewBounds.clear();
    extraction.culledSprites = 0;
    const UVec2 targetSize = extraction.targetSize;
    if (targetSize.x == 0 || targetSize.y == 0) {
        return;
    }

    const auto time = static_cast<f32>(context.resource<const Time>().elapsedSeconds());
    for (const auto& [entity, camera, global] : context.view<const Camera, const GlobalTransform>()) {
        extraction.packet.views.push_back(renderer::renderView(camera, global.matrix, targetSize, time));
        extraction.viewBounds.push_back(viewBounds(extraction.packet.views.back().camera));
    }
    if (extraction.viewBounds.empty()) {
        return;
    }

    for (const auto& [entity, sprite, global] : context.view<const SpriteRenderer, const GlobalTransform>()) {
        if (!sprite.visible) {
            continue;
        }
        const Aabb bounds = spriteBounds(sprite, global.matrix);
        bool seen = false;
        for (const Aabb& view : extraction.viewBounds) {
            if (math::intersects(bounds, view)) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            ++extraction.culledSprites;
            continue;
        }
        extraction.packet.sprites.push_back({
            .world = global.matrix,
            .uv = sprite.uv,
            .size = sprite.size,
            .pivot = sprite.pivot,
            .tint = sprite.tint,
            .texture = sprite.texture,
            .blend = sprite.blend,
            .layer = sprite.layer,
            .sortDepth = sprite.sortDepth,
        });
    }
}

} // namespace teserra::scene
