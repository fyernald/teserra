// The sprite batcher's CPU side: sort keys order layers, then depth, then batch; sorted sprites make the
// fewest batches, which break at texture, blend and capacity changes; equal keys keep their order; vertices
// carry the full 3D transform, top-left UVs and the tint; and a steady-state frame allocates nothing.

#include <teserra/renderer/sprite_batcher.hpp>

#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

rhi::TextureHandle texture(u32 index) {
    return rhi::TextureHandle{index, 1};
}

SpriteDrawItem sprite(rhi::TextureHandle texture, i16 layer = 0, f32 sortDepth = 0.0F) {
    return {.texture = texture, .layer = layer, .sortDepth = sortDepth};
}

std::vector<SpriteVertex> vertices(const SpriteBatcher& batcher, std::span<const SpriteDrawItem> sprites) {
    std::vector<std::byte> bytes(batcher.vertexBytes());
    batcher.writeVertices(sprites, bytes);
    std::vector<SpriteVertex> result(bytes.size() / sizeof(SpriteVertex));
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

bool near(const Vec3& actual, const Vec3& expected) {
    return std::abs(actual.x - expected.x) < 1.0e-5F && std::abs(actual.y - expected.y) < 1.0e-5F &&
           std::abs(actual.z - expected.z) < 1.0e-5F;
}

} // namespace

TEST_CASE("Sprite sort keys order layers, then depth, then texture and blend", "[renderer][sprites]") {
    const auto key = [](const SpriteDrawItem& item) { return spriteSortKey(item); };

    CHECK(key(sprite(texture(9), -2)) < key(sprite(texture(0), -1))); // negative layers first
    CHECK(key(sprite(texture(9), -1)) < key(sprite(texture(0), 0)));
    CHECK(key(sprite(texture(9), 0, -3.5F)) < key(sprite(texture(0), 0, -1.0F))); // negative depths
    CHECK(key(sprite(texture(9), 0, -1.0F)) < key(sprite(texture(0), 0, 0.25F)));
    CHECK(key(sprite(texture(9), 0, 0.25F)) < key(sprite(texture(0), 0, 8.0F)));
    CHECK(key(sprite(texture(1))) < key(sprite(texture(2)))); // then the batch id
    SpriteDrawItem additive = sprite(texture(1));
    additive.blend = SpriteBlend::Additive;
    CHECK(key(sprite(texture(1))) != key(additive));
}

TEST_CASE("Sorted sprites make one batch per texture in a layer", "[renderer][sprites]") {
    // 1000 sprites over 16 textures, interleaved, in one layer: 16 batches, one per texture.
    std::vector<SpriteDrawItem> sprites;
    sprites.reserve(1000);
    for (u32 i = 0; i < 1000; ++i) {
        sprites.push_back(sprite(texture((i * 7) % 16)));
    }
    SpriteBatcher batcher;
    batcher.prepare(sprites);

    REQUIRE(batcher.batches().size() == 16);
    u32 next = 0;
    for (const SpriteBatch& batch : batcher.batches()) {
        CHECK(batch.firstSprite == next); // contiguous, in draw order
        next += batch.spriteCount;
        for (u32 i = batch.firstSprite; i < batch.firstSprite + batch.spriteCount; ++i) {
            CHECK(sprites[batcher.order()[i]].texture == batch.texture);
        }
    }
    CHECK(next == 1000);

    // Layers come first: two layers with the same two textures need four batches.
    const std::array layered{sprite(texture(1), 1), sprite(texture(2), 0), sprite(texture(1), 0),
                             sprite(texture(2), 1)};
    batcher.prepare(layered);
    CHECK(batcher.batches().size() == 4);
    CHECK(batcher.order()[0] == 2); // layer 0, texture 1
}

TEST_CASE("A batch breaks at its capacity and at a blend change", "[renderer][sprites]") {
    SpriteBatcher batcher(3);
    const std::vector<SpriteDrawItem> sprites(7, sprite(texture(4)));
    batcher.prepare(sprites);
    REQUIRE(batcher.batches().size() == 3);
    CHECK(batcher.batches()[0].spriteCount == 3);
    CHECK(batcher.batches()[1].spriteCount == 3);
    CHECK(batcher.batches()[2].spriteCount == 1);
    CHECK(batcher.batches()[2].firstSprite == 6);

    std::array blended{sprite(texture(4)), sprite(texture(4))};
    blended[1].blend = SpriteBlend::Premultiplied;
    SpriteBatcher unlimited;
    unlimited.prepare(blended);
    REQUIRE(unlimited.batches().size() == 2);
    CHECK(unlimited.batches()[1].blend == SpriteBlend::Premultiplied);
}

TEST_CASE("Sprites with equal keys keep their order", "[renderer][sprites]") {
    std::vector<SpriteDrawItem> sprites(50, sprite(texture(3)));
    for (usize i = 0; i < sprites.size(); ++i) {
        sprites[i].size = Vec2{static_cast<f32>(i)}; // tells them apart without changing the key
    }
    SpriteBatcher batcher;
    batcher.prepare(sprites);
    for (u32 i = 0; i < sprites.size(); ++i) {
        CHECK(batcher.order()[i] == i);
    }
}

TEST_CASE("Sprite vertices: the world transform, top-left UVs and the tint", "[renderer][sprites]") {
    // A 2 x 1 sprite around its center, turned 90 degrees about Z and moved to (5, 0, 2).
    Mat4 world{1.0F};
    world[0] = Vec4{0.0F, 1.0F, 0.0F, 0.0F}; // x turns into +y
    world[1] = Vec4{-1.0F, 0.0F, 0.0F, 0.0F};
    world[3] = Vec4{5.0F, 0.0F, 2.0F, 1.0F};
    const std::array sprites{SpriteDrawItem{
        .world = world,
        .uv = {.min = {0.25F, 0.5F}, .max = {0.75F, 1.0F}},
        .size = {2.0F, 1.0F},
        .tint = {.r = 1.0F, .g = 0.5F, .b = 0.0F, .a = 0.25F},
        .texture = texture(1),
    }};
    SpriteBatcher batcher;
    batcher.prepare(sprites);
    const std::vector<SpriteVertex> quad = vertices(batcher, sprites);

    REQUIRE(quad.size() == 4);
    // Local corners top-left (-1, 0.5), bottom-left (-1, -0.5), bottom-right (1, -0.5), top-right (1, 0.5),
    // turned (x, y) -> (-y, x) and moved; z is kept.
    CHECK(near(quad[0].position, {4.5F, -1.0F, 2.0F}));
    CHECK(near(quad[1].position, {5.5F, -1.0F, 2.0F}));
    CHECK(near(quad[2].position, {5.5F, 1.0F, 2.0F}));
    CHECK(near(quad[3].position, {4.5F, 1.0F, 2.0F}));
    CHECK(quad[0].uv == Vec2{0.25F, 0.5F}); // the region's top-left corner
    CHECK(quad[1].uv == Vec2{0.25F, 1.0F});
    CHECK(quad[2].uv == Vec2{0.75F, 1.0F});
    CHECK(quad[3].uv == Vec2{0.75F, 0.5F});
    for (const SpriteVertex& vertex : quad) {
        CHECK(vertex.color == std::array<u8, 4>{255, 128, 0, 64});
    }
}

TEST_CASE("Sprite vertices follow the pivot", "[renderer][sprites]") {
    const std::array sprites{SpriteDrawItem{.size = {4.0F, 2.0F}, .pivot = {0.0F, 1.0F}}}; // bottom-left
    SpriteBatcher batcher;
    batcher.prepare(sprites);
    const std::vector<SpriteVertex> quad = vertices(batcher, sprites);
    REQUIRE(quad.size() == 4);
    CHECK(near(quad[0].position, {0.0F, 2.0F, 0.0F})); // top-left
    CHECK(near(quad[2].position, {4.0F, 0.0F, 0.0F})); // bottom-right
}

TEST_CASE("Sprite batching allocates nothing in steady state", "[renderer][sprites][allocation]") {
    std::vector<SpriteDrawItem> sprites;
    sprites.reserve(10'000);
    for (u32 i = 0; i < 10'000; ++i) {
        sprites.push_back(sprite(texture(i % 16), static_cast<i16>(i % 3)));
    }
    std::vector<std::byte> vertexMemory(sprites.size() * 4 * sizeof(SpriteVertex));
    SpriteBatcher batcher;
    batcher.prepare(sprites); // warm-up: the arrays grow once
    batcher.writeVertices(sprites, vertexMemory);

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        batcher.prepare(sprites);
        batcher.writeVertices(sprites, vertexMemory);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(batcher.batches().size() == usize{16} * 3); // three layers, sixteen textures each
}
