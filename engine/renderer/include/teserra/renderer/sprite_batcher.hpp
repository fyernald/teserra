#pragma once

// The CPU side of the sprite batcher (docs/architecture/renderer.md#sprite-batcher): sprites are sorted by a
// 64-bit key, grouped into batches that share a texture and a blend mode, and expanded into four vertices
// each. A steady-state frame allocates nothing.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/rhi/format.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/rhi/pipeline.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace teserra::renderer {

/// The 64-bit key that orders sprites: the layer (16 bits), sortDepth quantized to 24 bits, then a batch
/// id of the texture, material instance and blend (24 bits), so sprites that may share a batch end up next
/// to each other. An ascending key is the draw order.
[[nodiscard]] u64 spriteSortKey(const SpriteDrawItem& sprite) noexcept;

/// A vertex of a sprite quad, as the sprite shaders read it.
struct SpriteVertex {
    Vec3 position{0.0F};
    Vec2 uv{0.0F};
    /// The tint as RGBA bytes, linear, read as 0..1 floats (Unorm8x4).
    std::array<u8, 4> color{};
};
static_assert(sizeof(SpriteVertex) == 24);

/// The sprite vertex layout as RHI data: one buffer of SpriteVertex.
inline constexpr rhi::VertexBufferLayout kSpriteVertexBuffer{.stride = sizeof(SpriteVertex)};
/// Locations 0, 1 and 2: position (Float3), uv (Float2), color (Unorm8x4).
inline constexpr std::array<rhi::VertexAttribute, 3> kSpriteVertexAttributes{{
    {.location = 0, .format = rhi::VertexFormat::Float3, .offset = 0},
    {.location = 1, .format = rhi::VertexFormat::Float2, .offset = 12},
    {.location = 2, .format = rhi::VertexFormat::Unorm8x4, .offset = 20},
}};

/// Sprites per batch with 16-bit indices: 4 vertices each, below 65 536.
inline constexpr u32 kMaxSpritesPerBatch = 16383;

/// Consecutive sprites in draw order that one draw call can render.
struct SpriteBatch {
    rhi::TextureHandle texture{};
    SpriteBlend blend = SpriteBlend::Alpha;
    /// The material instance of the batch's sprites; null for sprite_unlit.
    const MaterialInstance* material = nullptr;
    /// Index of the batch's first sprite in draw order; its first vertex is firstSprite * 4.
    u32 firstSprite = 0;
    u32 spriteCount = 0;
};

/// Sorts and batches a frame's sprites and writes their vertices. Reuse one batcher: its arrays keep their
/// capacity, so frames after the largest one allocate nothing.
class SpriteBatcher {
public:
    /// `maxSpritesPerBatch` is at most kMaxSpritesPerBatch; tests use smaller ones.
    explicit SpriteBatcher(u32 maxSpritesPerBatch = kMaxSpritesPerBatch);

    /// Sorts `sprites` by spriteSortKey() (equal keys keep their order) and builds the batches. A batch
    /// breaks where the texture, blend or material instance changes, or when it is full.
    void prepare(std::span<const SpriteDrawItem> sprites);

    /// The batches of the last prepare(), in draw order.
    [[nodiscard]] std::span<const SpriteBatch> batches() const noexcept { return m_batches; }
    /// Indices into the prepared sprites, in draw order.
    [[nodiscard]] std::span<const u32> order() const noexcept { return m_order; }
    /// The bytes writeVertices() writes for the prepared sprites.
    [[nodiscard]] usize vertexBytes() const noexcept { return m_order.size() * 4 * sizeof(SpriteVertex); }

    /// Writes four vertices per sprite in draw order into `destination`, which holds at least vertexBytes():
    /// top-left, bottom-left, bottom-right and top-right corner, counter-clockwise with +Y up for the index
    /// pattern 0 1 2, 0 2 3. `sprites` are the ones given to prepare().
    void writeVertices(std::span<const SpriteDrawItem> sprites, std::span<std::byte> destination) const;

private:
    struct Keyed {
        u64 key = 0;
        u32 index = 0;
    };

    u32 m_maxSpritesPerBatch;
    std::vector<Keyed> m_keys;
    std::vector<u32> m_order;
    std::vector<SpriteBatch> m_batches;
};

} // namespace teserra::renderer
