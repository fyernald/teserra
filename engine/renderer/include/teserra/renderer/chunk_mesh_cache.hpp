#pragma once

// Static meshes of tilemap chunks (docs/architecture/renderer.md#tilemaps): each chunk's quads live in a
// GPU-only vertex buffer that is written when the chunk is built and reused every frame until it changes.
// The scene layer builds chunks during extraction; the sprite pass draws them as TilemapChunkDrawItems.
//
//   const ChunkMeshKey key{.scope = &extraction, .owner = entityBits, .layer = 0, .chunk = {2, -1}};
//   const ChunkMesh* mesh = cache.find(key);
//   if (mesh == nullptr || changed) {
//       mesh = cache.build(key, vertices, world, tag);   // writes the vertex buffer
//   }

#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/sprite_batcher.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>

#include <cstddef>
#include <functional>
#include <span>
#include <unordered_map>

namespace teserra::renderer {

/// Names one chunk mesh: a chunk of a layer of a tilemap owned by `owner` (an entity) in `scope` (its
/// world).
struct ChunkMeshKey {
    const void* scope = nullptr;
    u64 owner = 0;
    u32 layer = 0;
    IVec2 chunk{0};

    friend bool operator==(const ChunkMeshKey&, const ChunkMeshKey&) = default;
};

/// A built chunk: its quads as SpriteVertex quads (four vertices each, in the corner order of
/// SpriteBatcher::writeVertices()), in world space.
struct ChunkMesh {
    rhi::BufferHandle vertexBuffer{};
    u32 quadCount = 0;
    /// The world matrix the vertices were placed with.
    Mat4 world{1.0F};
    /// What the builder made the vertices from (tileset, tint, tile size), so it can tell when to rebuild.
    u64 tag = 0;
};

/// Owns the vertex buffers of tilemap chunk meshes. A mesh no build() or find() used for `evictAfterFrames`
/// frames is destroyed (its tilemap went away or has not been seen), so a builder must check find() and
/// rebuild what is missing. A steady-state frame, which builds nothing new, allocates nothing.
class ChunkMeshCache {
public:
    explicit ChunkMeshCache(rhi::Device& device, u32 evictAfterFrames = 300);
    ~ChunkMeshCache();

    ChunkMeshCache(const ChunkMeshCache&) = delete;
    ChunkMeshCache& operator=(const ChunkMeshCache&) = delete;
    ChunkMeshCache(ChunkMeshCache&&) = delete;
    ChunkMeshCache& operator=(ChunkMeshCache&&) = delete;

    /// Starts a frame: meshes unused for evictAfterFrames frames are destroyed.
    void beginFrame();

    /// The mesh of `key`, marked as used this frame; nullptr if it was never built or was evicted.
    [[nodiscard]] const ChunkMesh* find(const ChunkMeshKey& key) noexcept;

    /// Builds the mesh of `key` from `vertices` (four per quad) placed with `world`, writing its vertex
    /// buffer; the buffer is reused while it is large enough. Fails if a buffer cannot be created.
    [[nodiscard]] Result<const ChunkMesh*>
    build(const ChunkMeshKey& key, std::span<const SpriteVertex> vertices, const Mat4& world, u64 tag);

    /// Meshes alive.
    [[nodiscard]] usize size() const noexcept { return m_meshes.size(); }
    /// build() calls since the cache was created: how often chunks were rebuilt.
    [[nodiscard]] u64 buildCount() const noexcept { return m_buildCount; }

private:
    struct KeyHash {
        [[nodiscard]] usize operator()(const ChunkMeshKey& key) const noexcept;
    };
    struct Entry {
        ChunkMesh mesh;
        /// Vertices the buffer has room for.
        u32 capacity = 0;
        u64 lastUsedFrame = 0;
    };

    rhi::Device& m_device;
    u32 m_evictAfterFrames;
    u64 m_frame = 0;
    u64 m_buildCount = 0;
    std::unordered_map<ChunkMeshKey, Entry, KeyHash> m_meshes;
};

} // namespace teserra::renderer
