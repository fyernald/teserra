#pragma once

// Tile queries for gameplay (docs/architecture/renderer.md#tilemaps): what is at a world position, which
// tiles a box overlaps, and conversion between world positions and tile coordinates. Queries do not allocate.
//
//   const TilemapQuery query(map, world.get<GlobalTransform>(level).matrix);
//   const TileId under = query.tileAt(*map.findLayer("ground"), feet);
//   query.tilesOverlapping(ground, playerBox, [&](IVec2 coord, TileId tile) { ... });

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/scene/tilemap.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace teserra::scene {

/// The tile that holds `local`, a point in the tilemap's local space: tile (x, y) holds
/// [x, x + 1) x [y, y + 1) times the tile size.
[[nodiscard]] IVec2 localToTile(const Tilemap& map, Vec2 local) noexcept;

/// A tilemap placed in the world by its entity's world matrix (GlobalTransform::matrix). Holds a pointer to
/// the map, which must outlive the query; make one per frame or per system run, since the map's entity
/// may move. The map's local Z is ignored: tiles extend through all of it.
class TilemapQuery {
public:
    TilemapQuery(const Tilemap& map, const Mat4& world) noexcept;

    /// The tile coordinate at `worldPos`.
    [[nodiscard]] IVec2 worldToTile(Vec3 worldPos) const noexcept;

    /// The world position of the center of tile `coord`, at the map's local Z = 0.
    [[nodiscard]] Vec3 tileCenter(IVec2 coord) const noexcept;

    /// The world bounds of tile `coord`: its local square at Z = 0, transformed (and enclosed in an Aabb when
    /// the map is rotated).
    [[nodiscard]] Aabb tileBounds(IVec2 coord) const noexcept;

    /// The tile of `layer` at `worldPos`; kNoTile where there is none.
    [[nodiscard]] TileId tileAt(const TilemapLayer& layer, Vec3 worldPos) const noexcept {
        return layer.tile(worldToTile(worldPos));
    }

    /// The range of tile coordinates `worldBox` overlaps, as {min, max} (both inclusive). A tile counts when
    /// the box overlaps its inside; a box that only touches a tile's edge does not reach it. A box with no
    /// extent on an axis gets the tile its coordinate is in, like worldToTile(). A rotated map uses the
    /// local bounds of the box, which may cover more tiles.
    [[nodiscard]] std::array<IVec2, 2> tileRange(const Aabb& worldBox) const noexcept;

    /// Calls f(IVec2 coord, TileId tile) for every tile of `layer` that is not kNoTile in
    /// tileRange(worldBox), row by row from the bottom left. Empty chunks are skipped whole.
    template <typename F>
    void tilesOverlapping(const TilemapLayer& layer, const Aabb& worldBox, F&& f) const {
        const auto [first, last] = tileRange(worldBox);
        const IVec2 firstChunk = chunkOf(first);
        const IVec2 lastChunk = chunkOf(last);
        for (i32 chunkY = firstChunk.y; chunkY <= lastChunk.y; ++chunkY) {
            for (i32 chunkX = firstChunk.x; chunkX <= lastChunk.x; ++chunkX) {
                const TilemapChunk* chunk = layer.chunk({chunkX, chunkY});
                if (chunk == nullptr || chunk->tileCount == 0) {
                    continue;
                }
                const IVec2 origin{chunkX * kTilemapChunkSize, chunkY * kTilemapChunkSize};
                const i32 yBegin = std::max(first.y, origin.y);
                const i32 yEnd = std::min(last.y, origin.y + kTilemapChunkSize - 1);
                const i32 xBegin = std::max(first.x, origin.x);
                const i32 xEnd = std::min(last.x, origin.x + kTilemapChunkSize - 1);
                for (i32 y = yBegin; y <= yEnd; ++y) {
                    for (i32 x = xBegin; x <= xEnd; ++x) {
                        const usize index = (static_cast<usize>(y - origin.y) * kTilemapChunkSize) +
                                            static_cast<usize>(x - origin.x);
                        const TileId tile = chunk->tiles[index];
                        if (tile != kNoTile) {
                            f(IVec2{x, y}, tile);
                        }
                    }
                }
            }
        }
    }

    /// Whether `pred(TileId)` holds for any tile of `layer` that is not kNoTile in tileRange(worldBox).
    template <typename Pred>
    [[nodiscard]] bool anyOverlapping(const TilemapLayer& layer, const Aabb& worldBox, Pred&& pred) const {
        bool found = false;
        tilesOverlapping(layer, worldBox, [&](IVec2, TileId tile) { found = found || pred(tile); });
        return found;
    }

    /// The map the query reads.
    [[nodiscard]] const Tilemap& map() const noexcept { return *m_map; }

private:
    const Tilemap* m_map;
    Mat4 m_world;
    Mat4 m_toLocal;
};

} // namespace teserra::scene
