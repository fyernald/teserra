#include <teserra/scene/render_extraction.hpp>

#include <teserra/core/log.hpp>
#include <teserra/renderer/debug_draw.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <span>

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

bool seenByAnyView(const Aabb& bounds, const std::vector<Aabb>& views) noexcept {
    return std::ranges::any_of(views, [&](const Aabb& view) { return math::intersects(bounds, view); });
}

u8 unorm8(f32 value) noexcept {
    return static_cast<u8>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

u64 hashCombine(u64 seed, u64 value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

// What a tilemap's chunk meshes are made from besides their tiles and placement: a change rebuilds them.
u64 meshTag(const TilemapRenderer& tilemap, rhi::TextureHandle texture, Vec2 tileSize) noexcept {
    u64 tag = std::hash<const void*>{}(tilemap.tileset);
    tag = hashCombine(tag, std::hash<rhi::TextureHandle>{}(texture));
    for (const f32 value :
         {tilemap.tint.r, tilemap.tint.g, tilemap.tint.b, tilemap.tint.a, tileSize.x, tileSize.y}) {
        tag = hashCombine(tag, std::bit_cast<u32>(value));
    }
    return tag;
}

// Writes the quads of the tiles of `chunk` at `chunkCoord`, placed by `world`, in the corner order of
// SpriteBatcher::writeVertices(). UVs are pulled in by a fraction of a texel, so a sample at a tile's edge
// never reaches the neighboring tile of the sheet.
void writeChunkVertices(const TilemapChunk& chunk, IVec2 chunkCoord, Vec2 tileSize, const Mat4& world,
                        const Tileset& tileset, Vec2 uvInset, std::array<u8, 4> color,
                        std::vector<renderer::SpriteVertex>& vertices) {
    vertices.clear();
    const IVec2 origin = chunkCoord * kTilemapChunkSize;
    for (i32 y = 0; y < kTilemapChunkSize; ++y) {
        for (i32 x = 0; x < kTilemapChunkSize; ++x) {
            const TileId tile =
                chunk.tiles[(static_cast<usize>(y) * kTilemapChunkSize) + static_cast<usize>(x)];
            if (!tileset.contains(tile)) {
                continue; // no tile, or an id the tileset does not have
            }
            // Corners from integer tile coordinates, so neighboring tiles share their edges exactly.
            const Vec2 min = Vec2{origin + IVec2{x, y}} * tileSize;
            const Vec2 max = Vec2{origin + IVec2{x + 1, y + 1}} * tileSize;
            const renderer::UvRect uv = tileset.uv(tile);
            const Vec2 uvMin = uv.min + uvInset;
            const Vec2 uvMax = uv.max - uvInset;
            const auto corner = [&](f32 px, f32 py, f32 u, f32 v) {
                vertices.push_back(
                    {.position = Vec3{world * Vec4{px, py, 0.0F, 1.0F}}, .uv = {u, v}, .color = color});
            };
            // Top-left, bottom-left, bottom-right, top-right; UVs have their origin at the top-left.
            corner(min.x, max.y, uvMin.x, uvMin.y);
            corner(min.x, min.y, uvMin.x, uvMax.y);
            corner(max.x, min.y, uvMax.x, uvMax.y);
            corner(max.x, max.y, uvMax.x, uvMin.y);
        }
    }
}

void extractTilemaps(ecs::SystemContext& context, RenderExtraction& extraction) {
    renderer::ChunkMeshCache& cache = *extraction.chunkMeshes;
    for (auto [entity, map, tilemap, global] :
         context.view<Tilemap, const TilemapRenderer, const GlobalTransform>()) {
        if (!tilemap.visible || tilemap.tileset == nullptr) {
            continue;
        }
        const assets::Asset<renderer::Texture>& texture = tilemap.tileset->sheet().texture();
        if (!texture.isReady()) {
            continue;
        }
        const rhi::TextureHandle textureHandle = texture.get().handle();
        const Vec2 uvInset =
            texture.isFallback() || texture.get().size().x == 0 || texture.get().size().y == 0
                ? Vec2{0.0F}
                : Vec2{1.0F / 64.0F} / Vec2{texture.get().size()};
        const std::array<u8, 4> color{unorm8(tilemap.tint.r), unorm8(tilemap.tint.g), unorm8(tilemap.tint.b),
                                      unorm8(tilemap.tint.a)};
        const u64 tag = meshTag(tilemap, textureHandle, map.tileSize);
        const u64 owner = std::hash<ecs::Entity>{}(entity);

        for (u32 layerIndex = 0; layerIndex < map.layers.size(); ++layerIndex) {
            TilemapLayer& layer = map.layers[layerIndex];
            const auto drawLayer = static_cast<i16>(
                std::clamp<i32>(tilemap.layer + (static_cast<i32>(layerIndex) * tilemap.layerSpacing),
                                std::numeric_limits<i16>::min(), std::numeric_limits<i16>::max()));
            layer.forEachChunk([&](IVec2 chunkCoord, const TilemapChunk& chunk) {
                if (!seenByAnyView(chunkBounds(chunkCoord, map.tileSize, global.matrix),
                                   extraction.viewBounds)) {
                    ++extraction.culledChunks;
                    return; // built when it comes into view; a dirty chunk stays dirty until then
                }
                const renderer::ChunkMeshKey key{
                    .scope = &extraction, .owner = owner, .layer = layerIndex, .chunk = chunkCoord};
                const renderer::ChunkMesh* mesh = cache.find(key);
                if (mesh == nullptr || chunk.dirty || mesh->tag != tag || mesh->world != global.matrix) {
                    writeChunkVertices(chunk, chunkCoord, map.tileSize, global.matrix, *tilemap.tileset,
                                       uvInset, color, extraction.chunkVertices);
                    Result<const renderer::ChunkMesh*> built =
                        cache.build(key, extraction.chunkVertices, global.matrix, tag);
                    if (!built) {
                        TESERRA_LOG_ERROR(LogCategory::Renderer, "Cannot build tilemap chunk ({}, {}): {}",
                                          chunkCoord.x, chunkCoord.y, built.error());
                        return;
                    }
                    mesh = *built;
                    ++extraction.builtChunks;
                    layer.clearDirty(chunkCoord);
                }
                if (mesh->quadCount > 0) {
                    extraction.packet.tilemapChunks.push_back({.vertexBuffer = mesh->vertexBuffer,
                                                               .quadCount = mesh->quadCount,
                                                               .texture = textureHandle,
                                                               .blend = tilemap.blend,
                                                               .layer = drawLayer});
                }
            });
        }
    }
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

Aabb chunkBounds(IVec2 chunkCoord, Vec2 tileSize, const Mat4& world) noexcept {
    const Vec2 min = Vec2{chunkCoord * kTilemapChunkSize} * tileSize;
    const Vec2 max = Vec2{(chunkCoord + IVec2{1}) * kTilemapChunkSize} * tileSize;
    Aabb box = emptyBox();
    for (const Vec2 corner : std::array{min, Vec2{min.x, max.y}, max, Vec2{max.x, min.y}}) {
        grow(box, Vec3{world * Vec4{corner, 0.0F, 1.0F}});
    }
    return box;
}

void renderExtractionSystem(ecs::SystemContext& context) {
    RenderExtraction& extraction = context.resource<RenderExtraction>();
    extraction.packet.clear();
    extraction.viewBounds.clear();
    extraction.culledSprites = 0;
    extraction.culledChunks = 0;
    extraction.builtChunks = 0;
    if (extraction.chunkMeshes != nullptr) {
        extraction.chunkMeshes->beginFrame();
    }
    // Debug lines live for one frame: they are taken into the packet (when it is rendered) and cleared.
    if (auto* debug = context.tryResource<renderer::DebugDrawList>()) {
        if (extraction.targetSize.x != 0 && extraction.targetSize.y != 0) {
            const std::span<const renderer::DebugVertex> lines = debug->vertices();
            extraction.packet.debugLines.insert(extraction.packet.debugLines.end(), lines.begin(),
                                                lines.end());
        }
        debug->clear();
    }
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
        if (!seenByAnyView(spriteBounds(sprite, global.matrix), extraction.viewBounds)) {
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

    if (extraction.chunkMeshes != nullptr) {
        extractTilemaps(context, extraction);
    }
}

} // namespace teserra::scene
