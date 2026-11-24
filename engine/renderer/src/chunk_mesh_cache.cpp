#include <teserra/renderer/chunk_mesh_cache.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/rhi/buffer.hpp>

#include <utility>

namespace teserra::renderer {

namespace {

// Buffers grow in steps of this many quads, so small edits that add a tile rarely need a new buffer.
constexpr u32 kQuadGranularity = 64;

usize combine(usize seed, usize value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

} // namespace

usize ChunkMeshCache::KeyHash::operator()(const ChunkMeshKey& key) const noexcept {
    usize hash = std::hash<const void*>{}(key.scope);
    hash = combine(hash, std::hash<u64>{}(key.owner));
    hash = combine(hash, key.layer);
    hash = combine(hash, (static_cast<usize>(static_cast<u32>(key.chunk.x)) << 32U) |
                             static_cast<u32>(key.chunk.y));
    return hash;
}

ChunkMeshCache::ChunkMeshCache(rhi::Device& device, u32 evictAfterFrames)
    : m_device(device), m_evictAfterFrames(evictAfterFrames) {
}

ChunkMeshCache::~ChunkMeshCache() {
    for (const auto& [key, entry] : m_meshes) {
        m_device.destroy(entry.mesh.vertexBuffer);
    }
}

void ChunkMeshCache::beginFrame() {
    ++m_frame;
    std::erase_if(m_meshes, [&](const auto& item) {
        const Entry& entry = item.second;
        if (m_frame - entry.lastUsedFrame <= m_evictAfterFrames) {
            return false;
        }
        m_device.destroy(entry.mesh.vertexBuffer); // the device keeps it while a frame in flight uses it
        return true;
    });
}

const ChunkMesh* ChunkMeshCache::find(const ChunkMeshKey& key) noexcept {
    const auto it = m_meshes.find(key);
    if (it == m_meshes.end()) {
        return nullptr;
    }
    it->second.lastUsedFrame = m_frame;
    return &it->second.mesh;
}

Result<const ChunkMesh*> ChunkMeshCache::build(const ChunkMeshKey& key,
                                               std::span<const SpriteVertex> vertices, const Mat4& world,
                                               u64 tag) {
    TESERRA_ASSERT(vertices.size() % 4 == 0, "A chunk mesh needs four vertices per quad, not {}",
                   vertices.size());
    Entry& entry = m_meshes[key];
    const auto vertexCount = static_cast<u32>(vertices.size());
    if (vertexCount > entry.capacity) {
        const u32 quads = (vertexCount / 4 + kQuadGranularity - 1) / kQuadGranularity * kQuadGranularity;
        Result<rhi::BufferHandle> buffer = m_device.createBuffer({
            .sizeInBytes = u64{quads} * 4 * sizeof(SpriteVertex),
            .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst,
            .debugName = "Tilemap chunk",
        });
        if (!buffer) {
            m_device.destroy(entry.mesh.vertexBuffer);
            m_meshes.erase(key);
            return Unexpected(std::move(buffer).error());
        }
        m_device.destroy(entry.mesh.vertexBuffer);
        entry.mesh.vertexBuffer = *buffer;
        entry.capacity = quads * 4;
    }
    m_device.writeBuffer(entry.mesh.vertexBuffer, 0, std::as_bytes(vertices));
    entry.mesh.quadCount = vertexCount / 4;
    entry.mesh.world = world;
    entry.mesh.tag = tag;
    entry.lastUsedFrame = m_frame;
    ++m_buildCount;
    return &entry.mesh;
}

} // namespace teserra::renderer
