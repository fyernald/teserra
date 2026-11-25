#include <teserra/renderer/sprite_batcher.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace teserra::renderer {

namespace {

// Bits of the sort key, from the top.
constexpr u32 kDepthBits = 24;
constexpr u32 kBatchBits = 24;
// The batch id: the texture's slot index, 6 bits of the material instance, and the 2 bits of the blend mode.
// The material bits only group sprites; batches compare the instances themselves.
constexpr u32 kBlendBits = 2;
constexpr u32 kMaterialBits = 6;
constexpr u32 kMaxTextureIndex = (1U << (kBatchBits - kMaterialBits - kBlendBits)) - 1;

u64 materialBits(const MaterialInstance* material) noexcept {
    // Instances are at least 8-byte aligned; the bits above that differ between them.
    return material == nullptr ? 0
                               : ((reinterpret_cast<std::uintptr_t>(material) >> 3U) % 63U) +
                                     1U; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

// The bits of a float, mapped so that unsigned order is numeric order: negative values have all bits
// flipped, positive ones only the sign bit.
u32 sortableBits(f32 value) noexcept {
    const auto bits = std::bit_cast<u32>(value);
    return (bits & 0x8000'0000U) != 0 ? ~bits : bits | 0x8000'0000U;
}

u8 unorm8(f32 value) noexcept {
    return static_cast<u8>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

} // namespace

u64 spriteSortKey(const SpriteDrawItem& sprite) noexcept {
    const u64 layer = static_cast<u16>(static_cast<i32>(sprite.layer) + 32768); // -32768 sorts first
    const u64 depth = sortableBits(sprite.sortDepth) >> (32 - kDepthBits);
    const u32 textureIndex = sprite.texture.index();
    TESERRA_ASSERT(textureIndex <= kMaxTextureIndex, "Sprite texture slot {} does not fit the sort key",
                   textureIndex);
    const u64 batch = (u64{textureIndex & kMaxTextureIndex} << (kMaterialBits + kBlendBits)) |
                      (materialBits(sprite.material) << kBlendBits) | static_cast<u64>(sprite.blend);
    return (layer << (kDepthBits + kBatchBits)) | (depth << kBatchBits) | batch;
}

SpriteBatcher::SpriteBatcher(u32 maxSpritesPerBatch)
    : m_maxSpritesPerBatch(maxSpritesPerBatch) {
    TESERRA_ASSERT(maxSpritesPerBatch > 0 && maxSpritesPerBatch <= kMaxSpritesPerBatch,
                   "SpriteBatcher: {} sprites per batch; it must be 1 to {}", maxSpritesPerBatch,
                   kMaxSpritesPerBatch);
}

void SpriteBatcher::prepare(std::span<const SpriteDrawItem> sprites) {
    m_keys.clear();
    for (u32 index = 0; index < sprites.size(); ++index) {
        m_keys.push_back({.key = spriteSortKey(sprites[index]), .index = index});
    }
    // The index breaks ties, so equal keys keep their order and the frame does not flicker.
    std::ranges::sort(m_keys, [](const Keyed& a, const Keyed& b) {
        return a.key != b.key ? a.key < b.key : a.index < b.index;
    });

    m_order.clear();
    m_batches.clear();
    for (const Keyed& keyed : m_keys) {
        const SpriteDrawItem& sprite = sprites[keyed.index];
        const auto position = static_cast<u32>(m_order.size());
        m_order.push_back(keyed.index);
        if (!m_batches.empty()) {
            SpriteBatch& last = m_batches.back();
            if (last.texture == sprite.texture && last.blend == sprite.blend &&
                last.material == sprite.material && last.spriteCount < m_maxSpritesPerBatch) {
                ++last.spriteCount;
                continue;
            }
        }
        m_batches.push_back({.texture = sprite.texture,
                             .blend = sprite.blend,
                             .material = sprite.material,
                             .firstSprite = position,
                             .spriteCount = 1});
    }
}

void SpriteBatcher::writeVertices(std::span<const SpriteDrawItem> sprites,
                                  std::span<std::byte> destination) const {
    TESERRA_ASSERT(destination.size() >= vertexBytes(),
                   "SpriteBatcher::writeVertices: {} bytes for {} bytes of vertices", destination.size(),
                   vertexBytes());
    if (destination.size() < vertexBytes()) {
        return;
    }
    // The image corners in UV order (top-left origin), in the vertex order of the index pattern.
    constexpr std::array<Vec2, 4> kCorners{Vec2{0.0F, 0.0F}, Vec2{0.0F, 1.0F}, Vec2{1.0F, 1.0F},
                                           Vec2{1.0F, 0.0F}};
    std::byte* out = destination.data();
    for (const u32 index : m_order) {
        const SpriteDrawItem& sprite = sprites[index];
        const std::array<u8, 4> color{unorm8(sprite.tint.r), unorm8(sprite.tint.g), unorm8(sprite.tint.b),
                                      unorm8(sprite.tint.a)};
        for (const Vec2& corner : kCorners) {
            // Local space has +Y up while the image's rows go down.
            const Vec2 local{(corner.x - sprite.pivot.x) * sprite.size.x,
                             (sprite.pivot.y - corner.y) * sprite.size.y};
            const SpriteVertex vertex{
                .position = Vec3(sprite.world * Vec4(local, 0.0F, 1.0F)),
                .uv = sprite.uv.min + (corner * (sprite.uv.max - sprite.uv.min)),
                .color = color,
            };
            std::memcpy(out, &vertex, sizeof(vertex));
            out += sizeof(vertex); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): checked above
        }
    }
}

} // namespace teserra::renderer
