#include <teserra/scene/tilemap.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <utility>

namespace teserra::scene {

namespace {

// Floor division by the chunk size, also for negative values.
i32 floorDivChunk(i32 value) noexcept {
    return value >= 0 ? value / kTilemapChunkSize : -((-value - 1) / kTilemapChunkSize) - 1;
}

u64 packChunk(IVec2 chunkCoord) noexcept {
    return (static_cast<u64>(static_cast<u32>(chunkCoord.x)) << 32U) | static_cast<u32>(chunkCoord.y);
}

usize tileIndex(IVec2 coord, IVec2 chunkCoord) noexcept {
    const i32 x = coord.x - (chunkCoord.x * kTilemapChunkSize);
    const i32 y = coord.y - (chunkCoord.y * kTilemapChunkSize);
    return (static_cast<usize>(y) * kTilemapChunkSize) + static_cast<usize>(x);
}

} // namespace

IVec2 chunkOf(IVec2 tile) noexcept {
    return {floorDivChunk(tile.x), floorDivChunk(tile.y)};
}

TilemapLayer::TilemapLayer(std::string name)
    : m_name(std::move(name)) {
}

TileId TilemapLayer::tile(IVec2 coord) const noexcept {
    const IVec2 chunkCoord = chunkOf(coord);
    const TilemapChunk* found = chunk(chunkCoord);
    return found != nullptr ? found->tiles[tileIndex(coord, chunkCoord)] : kNoTile;
}

void TilemapLayer::setTile(IVec2 coord, TileId tile) {
    const IVec2 chunkCoord = chunkOf(coord);
    const u64 key = packChunk(chunkCoord);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) {
        if (tile == kNoTile) {
            return;
        }
        it = m_chunks.emplace(key, Entry{.coord = chunkCoord, .chunk = {}}).first;
    }
    TilemapChunk& target = it->second.chunk;
    TileId& slot = target.tiles[tileIndex(coord, chunkCoord)];
    if (slot == tile) {
        return;
    }
    if (slot == kNoTile) {
        ++target.tileCount;
    } else if (tile == kNoTile) {
        TESERRA_ASSERT(target.tileCount > 0, "Tile count of a chunk out of sync");
        --target.tileCount;
    }
    slot = tile;
    target.dirty = true;
}

const TilemapChunk* TilemapLayer::chunk(IVec2 chunkCoord) const noexcept {
    const auto it = m_chunks.find(packChunk(chunkCoord));
    return it != m_chunks.end() ? &it->second.chunk : nullptr;
}

usize TilemapLayer::dirtyChunkCount() const noexcept {
    return static_cast<usize>(
        std::ranges::count_if(m_chunks, [](const auto& entry) { return entry.second.chunk.dirty; }));
}

void TilemapLayer::clearDirty(IVec2 chunkCoord) noexcept {
    const auto it = m_chunks.find(packChunk(chunkCoord));
    if (it != m_chunks.end()) {
        it->second.chunk.dirty = false;
    }
}

TilemapLayer& Tilemap::addLayer(std::string name) {
    return layers.emplace_back(std::move(name));
}

TilemapLayer* Tilemap::findLayer(std::string_view name) noexcept {
    const auto it =
        std::ranges::find_if(layers, [name](const TilemapLayer& layer) { return layer.name() == name; });
    return it != layers.end() ? &*it : nullptr;
}

const TilemapLayer* Tilemap::findLayer(std::string_view name) const noexcept {
    const auto it =
        std::ranges::find_if(layers, [name](const TilemapLayer& layer) { return layer.name() == name; });
    return it != layers.end() ? &*it : nullptr;
}

} // namespace teserra::scene
