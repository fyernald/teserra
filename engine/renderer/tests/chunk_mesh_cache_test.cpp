// ChunkMeshCache on the null backend: builds write GPU-only vertex buffers that are reused while large
// enough, find() marks meshes as used, and unused meshes are evicted.

#include <teserra/renderer/chunk_mesh_cache.hpp>

#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

std::vector<SpriteVertex> quads(u32 count, f32 x = 0.0F) {
    std::vector<SpriteVertex> vertices(usize{count} * 4);
    for (usize i = 0; i < vertices.size(); ++i) {
        vertices[i].position = {x + static_cast<f32>(i), 0.0F, 0.0F};
    }
    return vertices;
}

} // namespace

TEST_CASE("Chunk mesh cache: a build writes a GPU-only vertex buffer", "[renderer][tilemap]") {
    const std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    ChunkMeshCache cache(*device);
    const ChunkMeshKey key{.owner = 7, .layer = 1, .chunk = {-2, 3}};
    CHECK(cache.find(key) == nullptr);

    const std::vector<SpriteVertex> vertices = quads(3);
    const Result<const ChunkMesh*> built = cache.build(key, vertices, Mat4{2.0F}, 42);
    REQUIRE(built.has_value());
    const ChunkMesh& mesh = *built.value();
    CHECK(mesh.quadCount == 3);
    CHECK(mesh.tag == 42);
    CHECK(mesh.world == Mat4{2.0F});
    const rhi::BufferDesc* desc = device->buffer(mesh.vertexBuffer);
    REQUIRE(desc != nullptr);
    CHECK(desc->memory == rhi::MemoryLocation::GpuOnly);
    CHECK((desc->usage & rhi::BufferUsage::Vertex) == rhi::BufferUsage::Vertex);
    CHECK(desc->sizeInBytes >= usize{3} * 4 * sizeof(SpriteVertex));
    const std::span<const std::byte> data = device->bufferData(mesh.vertexBuffer);
    CHECK(std::memcmp(data.data(), vertices.data(), vertices.size() * sizeof(SpriteVertex)) == 0);

    CHECK(cache.find(key) == &mesh);
    CHECK(cache.find({.owner = 7, .layer = 1, .chunk = {-2, 4}}) == nullptr);
    CHECK(cache.find({.scope = &cache, .owner = 7, .layer = 1, .chunk = {-2, 3}}) == nullptr);
    CHECK(cache.size() == 1);
    CHECK(cache.buildCount() == 1);
}

TEST_CASE("Chunk mesh cache: rebuilds reuse the buffer while it is large enough", "[renderer][tilemap]") {
    const std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    ChunkMeshCache cache(*device);
    const ChunkMeshKey key{.owner = 1};
    const rhi::BufferHandle first = cache.build(key, quads(10), Mat4{1.0F}, 0).value()->vertexBuffer;
    const usize objects = device->liveObjectCount();

    const ChunkMesh* smaller = cache.build(key, quads(2, 5.0F), Mat4{1.0F}, 1).value();
    CHECK(smaller->vertexBuffer == first);
    CHECK(smaller->quadCount == 2);
    CHECK(device->liveObjectCount() == objects);

    const ChunkMesh* larger = cache.build(key, quads(1024), Mat4{1.0F}, 2).value();
    CHECK(larger->vertexBuffer != first); // grown: the old buffer is destroyed
    CHECK(larger->quadCount == 1024);
    CHECK(device->liveObjectCount() == objects);
    CHECK(device->buffer(first) == nullptr);
    CHECK(cache.buildCount() == 3);

    const ChunkMesh* empty = cache.build(key, {}, Mat4{1.0F}, 3).value();
    CHECK(empty->quadCount == 0);
}

TEST_CASE("Chunk mesh cache: meshes unused for a while are evicted", "[renderer][tilemap]") {
    const std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    ChunkMeshCache cache(*device, 2);
    const ChunkMeshKey used{.owner = 1};
    const ChunkMeshKey unused{.owner = 2};
    cache.beginFrame();
    static_cast<void>(cache.build(used, quads(1), Mat4{1.0F}, 0));
    const rhi::BufferHandle unusedBuffer = cache.build(unused, quads(1), Mat4{1.0F}, 0).value()->vertexBuffer;

    for (int frame = 0; frame < 3; ++frame) {
        cache.beginFrame();
        CHECK(cache.find(used) != nullptr);
    }
    CHECK(cache.size() == 1);
    CHECK(cache.find(unused) == nullptr);
    CHECK(device->buffer(unusedBuffer) == nullptr);
}

TEST_CASE("Chunk mesh cache: finding and starting frames allocate nothing",
          "[renderer][tilemap][allocation]") {
    const std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    ChunkMeshCache cache(*device);
    for (u64 owner = 0; owner < 16; ++owner) {
        static_cast<void>(cache.build({.owner = owner}, quads(4), Mat4{1.0F}, 0));
    }
    usize found = 0;
    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        cache.beginFrame();
        for (u64 owner = 0; owner < 16; ++owner) {
            found += cache.find({.owner = owner}) != nullptr ? 1U : 0U;
        }
        allocations = counter.count();
    }
    CHECK(found == 16);
    CHECK(allocations == 0);
}
