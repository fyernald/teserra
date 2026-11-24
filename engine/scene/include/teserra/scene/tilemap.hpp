#pragma once

// Tilemaps (docs/architecture/renderer.md#tilemaps): layers of tile ids stored in 32x32 chunks, so an edit
// touches one chunk and the renderer rebuilds only the chunks that changed. Chunks without tiles are never
// allocated, so sparse maps are cheap. This header is portable: it does not need the renderer.
//
//   Tilemap& map = level.add<Tilemap>(entity, Tilemap{.tileSize = {1.0F, 1.0F}, .layers = {}});
//   TilemapLayer& ground = map.addLayer("ground");
//   ground.setTile({-3, 0}, 5);          // tile 4 of the tileset (ids count from 1)
//   const TileId id = ground.tile({-3, 0});

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/type_key.hpp>

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace teserra::scene {

/// A tile of a tileset: 0 (kNoTile) is no tile, n >= 1 is tile n - 1 of the tileset (Tiled's convention,
/// without its flip bits).
using TileId = u32;
inline constexpr TileId kNoTile = 0;

/// Width and height of a chunk, in tiles.
inline constexpr i32 kTilemapChunkSize = 32;

/// 32 x 32 tiles of a layer. Tile (x, y) of the chunk is tiles[y * kTilemapChunkSize + x]; row 0 is at the
/// bottom (+Y up, ADR-0024).
struct TilemapChunk {
    std::array<TileId, static_cast<usize>(kTilemapChunkSize) * kTilemapChunkSize> tiles{};
    /// Tiles that are not kNoTile.
    u32 tileCount = 0;
    /// Changed since the renderer last built it.
    bool dirty = true;
};

/// The chunk that holds `tile`, by floor division: tile (-1, 0) is in chunk (-1, 0), tile (32, 0) in (1, 0).
[[nodiscard]] IVec2 chunkOf(IVec2 tile) noexcept;

/// One layer of a tilemap: a sparse grid of tile ids on integer coordinates, which may be negative.
// clang-tidy flags the implicit move constructor because MSVC's unordered_map allocates when it is moved;
// like the rest of the engine, the layer does not handle allocation failure.
class TilemapLayer { // NOLINT(bugprone-exception-escape)
public:
    explicit TilemapLayer(std::string name);

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }

    /// The tile at `coord`; kNoTile outside allocated chunks.
    [[nodiscard]] TileId tile(IVec2 coord) const noexcept;

    /// Sets the tile at `coord`. The first tile of a chunk allocates it; kNoTile never allocates. The chunk
    /// is marked dirty when the value changes. A chunk whose tiles are all removed stays allocated (dirty,
    /// with tileCount 0), so the renderer sees the change.
    void setTile(IVec2 coord, TileId tile);

    /// The chunk at chunk coordinate `chunkCoord` (see chunkOf()), or nullptr if it was never allocated.
    [[nodiscard]] const TilemapChunk* chunk(IVec2 chunkCoord) const noexcept;

    /// Calls f(IVec2 chunkCoord, const TilemapChunk&) for every allocated chunk, in no particular order.
    template <typename F>
    void forEachChunk(F&& f) const {
        for (const auto& [key, entry] : m_chunks) {
            f(entry.coord, entry.chunk);
        }
    }

    [[nodiscard]] usize chunkCount() const noexcept { return m_chunks.size(); }
    [[nodiscard]] usize dirtyChunkCount() const noexcept;

    /// Marks the chunk at `chunkCoord` as built; the renderer calls it after rebuilding the chunk. Does
    /// nothing for a chunk that is not allocated.
    void clearDirty(IVec2 chunkCoord) noexcept;

private:
    struct Entry {
        IVec2 coord{0};
        TilemapChunk chunk;
    };

    std::string m_name;
    std::unordered_map<u64, Entry> m_chunks;
};

/// A grid of tiles on an entity. Tile (x, y) covers [x, x + 1) x [y, y + 1) times tileSize, in the entity's
/// local space; the entity's transform places the map in the world.
struct Tilemap {
    /// Width and height of a tile, in world units.
    Vec2 tileSize{1.0F};
    /// Drawn in order: later layers on top.
    std::vector<TilemapLayer> layers;

    /// Adds a layer on top of the others. The reference is valid until the next addLayer().
    TilemapLayer& addLayer(std::string name);

    /// The first layer named `name`, or nullptr.
    [[nodiscard]] TilemapLayer* findLayer(std::string_view name) noexcept;
    [[nodiscard]] const TilemapLayer* findLayer(std::string_view name) const noexcept;
};

} // namespace teserra::scene

TESERRA_COMPONENT(teserra::scene::Tilemap, "Tilemap");
