// Tilemap extraction: chunks in view become TilemapChunkDrawItems with meshes from the ChunkMeshCache; only
// dirty, missing or moved chunks rebuild; chunks outside every view are culled; and a steady-state Extract
// allocates nothing.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/chunk_mesh_cache.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/render_extraction.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>
#include <teserra/scene/transform.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <vector>

using namespace teserra;
using namespace teserra::scene;

namespace {

// A scene with a camera at the origin showing x in [-10, 10] and y in [-5, 5] (200 x 100 target), a tileset
// of two 16x16 tiles, and a chunk mesh cache on the null device.
struct TilemapScene {
    TilemapScene() {
        const assets::Asset<renderer::Texture> texture =
            registry.load<renderer::Texture>("game://tiles", [&]() -> Result<renderer::Texture> {
                TESERRA_TRY_ASSIGN(const rhi::TextureHandle handle,
                                   device->createTexture({.width = 32, .height = 16}));
                return renderer::Texture(*device, handle, {32, 16});
            });
        tileset = std::make_unique<Tileset>(Tileset::grid(texture, {.cellSize = {16, 16}}).value());
        const ecs::Entity camera = level.spawn();
        level.add<Transform>(camera);
        level.add<Camera>(camera, Camera{.projection = Orthographic{.height = 10.0F}});
        extraction().chunkMeshes = &cache;
    }

    RenderExtraction& extraction() { return level.world().resource<RenderExtraction>(); }

    ecs::Entity addMap(Tilemap map, TilemapRenderer renderer = {}, Vec3 position = Vec3{0.0F}) {
        const ecs::Entity entity = level.spawn();
        level.add<Transform>(entity, Transform{.position = position});
        level.add<Tilemap>(entity, std::move(map));
        renderer.tileset = tileset.get();
        level.add<TilemapRenderer>(entity, renderer);
        return entity;
    }

    RenderExtraction& extract() {
        RenderExtraction& result = extraction();
        result.targetSize = {200U, 100U};
        level.run(ecs::Phase::PostUpdate);
        level.run(ecs::Phase::Extract);
        return result;
    }

    std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    assets::AssetRegistry registry;
    std::unique_ptr<Tileset> tileset;
    renderer::ChunkMeshCache cache{*device};
    Scene level{"level"};
};

// Tiles in the four chunks around the origin, one per chunk, and one far away in chunk (10, 10).
Tilemap fourChunks() {
    Tilemap map{.tileSize = {1.0F, 1.0F}};
    TilemapLayer& ground = map.addLayer("ground");
    ground.setTile({0, 0}, 1);
    ground.setTile({-1, 0}, 2);
    ground.setTile({0, -1}, 1);
    ground.setTile({-1, -1}, 2);
    ground.setTile({330, 330}, 1);
    return map;
}

} // namespace

TEST_CASE("Tilemap extraction: chunks in view are drawn, the rest culled", "[scene][render][tilemap]") {
    TilemapScene scene;
    const ecs::Entity map = scene.addMap(fourChunks(), {.layer = 3});

    const RenderExtraction& extraction = scene.extract();
    REQUIRE(extraction.packet.tilemapChunks.size() == 4);
    for (const renderer::TilemapChunkDrawItem& chunk : extraction.packet.tilemapChunks) {
        CHECK(chunk.quadCount == 1);
        CHECK(chunk.layer == 3);
        CHECK(chunk.texture == scene.tileset->sheet().texture().get().handle());
        CHECK_FALSE(chunk.vertexBuffer.isNull());
    }
    CHECK(extraction.culledChunks == 1);
    CHECK(extraction.builtChunks == 4);
    // Built chunks are clean; the culled one stays dirty until it is seen.
    const TilemapLayer& ground = scene.level.world().get<Tilemap>(map).layers[0];
    CHECK(ground.dirtyChunkCount() == 1);
    CHECK(ground.chunk({10, 10})->dirty);
    CHECK(ecs::componentName<TilemapRenderer>() == "TilemapRenderer");
}

TEST_CASE("Tilemap extraction: only dirty chunks rebuild", "[scene][render][tilemap]") {
    TilemapScene scene;
    const ecs::Entity map = scene.addMap(fourChunks());
    static_cast<void>(scene.extract());
    CHECK(scene.cache.buildCount() == 4);

    CHECK(scene.extract().builtChunks == 0); // nothing changed
    CHECK(scene.extract().packet.tilemapChunks.size() == 4);

    scene.level.world().get<Tilemap>(map).layers[0].setTile({5, 3}, 2); // chunk (0, 0)
    const RenderExtraction& edited = scene.extract();
    CHECK(edited.builtChunks == 1);
    CHECK(scene.cache.buildCount() == 5);
    usize quads = 0;
    for (const renderer::TilemapChunkDrawItem& chunk : edited.packet.tilemapChunks) {
        quads += chunk.quadCount;
    }
    CHECK(quads == 5);

    scene.level.world().get<Tilemap>(map).layers[0].setTile({5, 3}, 2); // the same value: nothing to do
    CHECK(scene.extract().builtChunks == 0);
}

TEST_CASE("Tilemap extraction: moving or tinting the map rebuilds its chunks", "[scene][render][tilemap]") {
    TilemapScene scene;
    const ecs::Entity map = scene.addMap(fourChunks());
    static_cast<void>(scene.extract());

    scene.level.world().get<Transform>(map).position.x = 0.5F;
    CHECK(scene.extract().builtChunks == 4);
    CHECK(scene.extract().builtChunks == 0);

    scene.level.world().get<TilemapRenderer>(map).tint.g = 0.5F;
    CHECK(scene.extract().builtChunks == 4);
}

TEST_CASE("Tilemap extraction: layers, spacing, visibility and no cache", "[scene][render][tilemap]") {
    TilemapScene scene;
    Tilemap tiles{.tileSize = {1.0F, 1.0F}};
    tiles.addLayer("ground").setTile({0, 0}, 1);
    tiles.addLayer("empty");
    tiles.addLayer("decor").setTile({1, 1}, 2);
    const ecs::Entity map = scene.addMap(std::move(tiles), {.layer = -5, .layerSpacing = 10});

    std::vector<i16> layers;
    for (const renderer::TilemapChunkDrawItem& chunk : scene.extract().packet.tilemapChunks) {
        layers.push_back(chunk.layer);
    }
    CHECK(layers == std::vector<i16>{-5, 15});

    scene.level.world().get<TilemapRenderer>(map).visible = false;
    CHECK(scene.extract().packet.tilemapChunks.empty());
    scene.level.world().get<TilemapRenderer>(map).visible = true;

    scene.extraction().chunkMeshes = nullptr;
    CHECK(scene.extract().packet.tilemapChunks.empty());
}

TEST_CASE("Tilemap extraction: chunk vertices are tile quads with inset UVs", "[scene][render][tilemap]") {
    TilemapScene scene;
    Tilemap tiles{.tileSize = {2.0F, 1.0F}};
    tiles.addLayer("ground").setTile({-1, 0}, 2); // the right half of the 32x16 texture
    static_cast<void>(scene.addMap(std::move(tiles), {}, {1.0F, 0.0F, 0.0F}));

    const RenderExtraction& extraction = scene.extract();
    REQUIRE(extraction.packet.tilemapChunks.size() == 1);
    const std::span<const std::byte> bytes =
        scene.device->bufferData(extraction.packet.tilemapChunks[0].vertexBuffer);
    std::array<renderer::SpriteVertex, 4> vertices{};
    REQUIRE(bytes.size() >= sizeof(vertices));
    std::memcpy(vertices.data(), bytes.data(), sizeof(vertices));

    // Tile (-1, 0) covers x in [-2, 0] and y in [0, 1] locally, moved right by 1.
    CHECK(vertices[0].position == Vec3{-1.0F, 1.0F, 0.0F}); // top-left
    CHECK(vertices[1].position == Vec3{-1.0F, 0.0F, 0.0F}); // bottom-left
    CHECK(vertices[2].position == Vec3{1.0F, 0.0F, 0.0F});  // bottom-right
    CHECK(vertices[3].position == Vec3{1.0F, 1.0F, 0.0F});  // top-right
    const f32 insetU = 1.0F / 64.0F / 32.0F;
    const f32 insetV = 1.0F / 64.0F / 16.0F;
    CHECK(vertices[0].uv.x == Catch::Approx(0.5F + insetU));
    CHECK(vertices[0].uv.y == Catch::Approx(insetV));
    CHECK(vertices[2].uv.x == Catch::Approx(1.0F - insetU));
    CHECK(vertices[2].uv.y == Catch::Approx(1.0F - insetV));
    CHECK(vertices[0].color == std::array<u8, 4>{255, 255, 255, 255});
}

TEST_CASE("Tilemap extraction: a steady-state Extract of a 256x256, 3-layer map allocates nothing",
          "[scene][render][tilemap][allocation]") {
    TilemapScene scene;
    Tilemap tiles{.tileSize = {1.0F, 1.0F}};
    for (const char* name : {"ground", "walls", "decor"}) {
        TilemapLayer& layer = tiles.addLayer(name);
        for (i32 y = -128; y < 128; ++y) {
            for (i32 x = -128; x < 128; ++x) {
                layer.setTile({x, y}, static_cast<TileId>(((x + y) & 1) + 1));
            }
        }
    }
    static_cast<void>(scene.addMap(std::move(tiles)));
    static_cast<void>(scene.extract()); // builds the chunks in view

    usize chunks = 0;
    usize culled = 0;
    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        const RenderExtraction& extraction = scene.extract();
        chunks = extraction.packet.tilemapChunks.size();
        culled = extraction.culledChunks;
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(chunks == 12); // the four chunks around the origin, in three layers
    CHECK(culled == (64 * 3) - 12);
}
