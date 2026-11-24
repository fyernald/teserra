#pragma once

// Tiled maps in a scene (docs/architecture/renderer.md#tilemaps): a TiledMap's tile layers as a Tilemap, its
// tileset as a Tileset, and its objects' rectangles in the map's local space.
//
//   const Asset<TiledMap> level = app.assets().load<TiledMap>("assets://maps/level1.tmj");
//   Tileset tileset = *tilesetFromTiled(level.get().tilesets[0], app.assets());
//   scene.add<Tilemap>(map, *tilemapFromTiled(level.get()));
//   scene.add<TilemapRenderer>(map, TilemapRenderer{.tileset = &tileset});
//
// Tiled counts rows down from the top in pixels; Teserra is +Y up in world units (ADR-0024). The map's
// bottom-left tile becomes tile (0, 0), and pixels are divided by pixelsPerUnit (16 by default).

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/tiled.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>

namespace teserra::scene {

/// The tile layers of `map` as a Tilemap, in the same order and with the same names. Tiled's column c of row
/// r becomes tile (c, height - 1 - r). Every tile must come from the tileset at `tilesetIndex`: tile id n
/// of the Tilemap is that tileset's tile n - 1. The tile size is the map's, in pixels, divided by
/// `pixelsPerUnit`.
///
/// Errors: InvalidArgument (no tileset at `tilesetIndex`, `pixelsPerUnit` not positive), Unsupported (a
/// tile of another tileset: one tileset per tilemap in v1), naming the layer and the Tiled tile.
[[nodiscard]] Result<Tilemap> tilemapFromTiled(const assets::TiledMap& map, usize tilesetIndex = 0,
                                               f32 pixelsPerUnit = renderer::kDefaultPixelsPerUnit);

/// `tileset` as a Tileset: its image loaded from `registry` as a texture, cut into the tileset's grid (tile
/// size, margin, spacing, tile count). A tile's bool property "solid" becomes TileProperties::solid. A
/// missing image gives the texture's fallback, like any texture. Errors: those of Tileset::grid().
[[nodiscard]] Result<Tileset> tilesetFromTiled(const assets::TiledTileset& tileset,
                                               assets::AssetRegistry& registry,
                                               f32 pixelsPerUnit = renderer::kDefaultPixelsPerUnit);

/// The rectangle of `object` in the map's local space (the space of tilemapFromTiled()'s tiles), at z = 0.
/// Rotation is ignored.
[[nodiscard]] Aabb tiledObjectBounds(const assets::TiledMap& map, const assets::TiledObject& object,
                                     f32 pixelsPerUnit = renderer::kDefaultPixelsPerUnit) noexcept;

} // namespace teserra::scene
