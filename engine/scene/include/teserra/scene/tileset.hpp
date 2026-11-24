#pragma once

// Tilesets (docs/architecture/renderer.md#tilemaps): the art and properties of the tiles a Tilemap's ids
// name. Tile id n >= 1 is region n - 1 of a sprite sheet.
//
//   Asset<Texture> texture = assets.load<Texture>("assets://tiles/terrain.png");
//   Tileset tileset = *Tileset::grid(texture, {.cellSize = {16, 16}}, {{}, {.solid = true}});
//   tileset.isSolid(2);           // true: tile 2 is the grid's second cell

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/tile_queries.hpp>
#include <teserra/scene/tilemap.hpp>

#include <vector>

namespace teserra::scene {

/// What gameplay knows about a tile. More properties come with Tiled custom properties (#90).
struct TileProperties {
    /// Blocks movement (tile queries and collision).
    bool solid = false;

    friend bool operator==(const TileProperties&, const TileProperties&) = default;
};

/// The tiles of a tilemap: tile id n >= 1 shows region n - 1 of the sheet and has properties[n - 1]. Tiles
/// without properties have the defaults. The tileset holds its sheet, and so its texture.
class Tileset {
public:
    /// A tileset of the regions of `sheet`. `properties` has at most one entry per region; the rest of the
    /// tiles get the defaults.
    explicit Tileset(renderer::SpriteSheet sheet, std::vector<TileProperties> properties = {});

    /// A tileset of the cells of `grid` (see renderer::SpriteSheet::grid() for the errors).
    [[nodiscard]] static Result<Tileset> grid(assets::Asset<renderer::Texture> texture,
                                              const renderer::SpriteGrid& grid,
                                              std::vector<TileProperties> properties = {},
                                              f32 pixelsPerUnit = renderer::kDefaultPixelsPerUnit);

    [[nodiscard]] const renderer::SpriteSheet& sheet() const noexcept { return m_sheet; }

    /// Number of tiles: valid ids are 1 to tileCount().
    [[nodiscard]] usize tileCount() const noexcept { return m_sheet.regions().size(); }

    /// Whether `tile` names a tile of this set (kNoTile does not).
    [[nodiscard]] bool contains(TileId tile) const noexcept { return tile != kNoTile && tile <= tileCount(); }

    /// The UVs of `tile`, top-left origin; asserts that the tileset contains it.
    [[nodiscard]] renderer::UvRect uv(TileId tile) const;

    /// The properties of `tile`; the defaults for kNoTile, unknown tiles and tiles without properties.
    [[nodiscard]] const TileProperties& properties(TileId tile) const noexcept;

    [[nodiscard]] bool isSolid(TileId tile) const noexcept { return properties(tile).solid; }

private:
    renderer::SpriteSheet m_sheet;
    std::vector<TileProperties> m_properties;
};

/// Whether the tile of `layer` at `worldPos` is solid in `tileset`.
[[nodiscard]] inline bool isSolidAt(const TilemapQuery& query, const TilemapLayer& layer,
                                    const Tileset& tileset, Vec3 worldPos) noexcept {
    return tileset.isSolid(query.tileAt(layer, worldPos));
}

/// Whether `worldBox` overlaps a solid tile of `layer` (see TilemapQuery::tileRange() for what overlaps).
[[nodiscard]] inline bool overlapsSolid(const TilemapQuery& query, const TilemapLayer& layer,
                                        const Tileset& tileset, const Aabb& worldBox) {
    return query.anyOverlapping(layer, worldBox, [&](TileId tile) { return tileset.isSolid(tile); });
}

} // namespace teserra::scene
