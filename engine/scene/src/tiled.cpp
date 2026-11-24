#include <teserra/scene/tiled.hpp>

#include <teserra/renderer/assets.hpp>

#include <utility>
#include <vector>

namespace teserra::scene {

Result<Tilemap> tilemapFromTiled(const assets::TiledMap& map, usize tilesetIndex, f32 pixelsPerUnit) {
    if (tilesetIndex >= map.tilesets.size()) {
        return fail(ErrorCode::InvalidArgument, Module::Scene, "The map has {} tilesets, not one at index {}",
                    map.tilesets.size(), tilesetIndex);
    }
    if (!(pixelsPerUnit > 0.0F)) {
        return fail(ErrorCode::InvalidArgument, Module::Scene, "pixelsPerUnit must be positive, not {}",
                    pixelsPerUnit);
    }
    const assets::TiledTileset& tileset = map.tilesets[tilesetIndex];

    Tilemap tilemap{.tileSize = Vec2{map.tileSize} / pixelsPerUnit};
    tilemap.layers.reserve(map.tileLayers.size());
    for (const assets::TiledTileLayer& source : map.tileLayers) {
        TilemapLayer& layer = tilemap.layers.emplace_back(source.name);
        for (u32 row = 0; row < source.size.y; ++row) {
            for (u32 column = 0; column < source.size.x; ++column) {
                const u32 gid = source.gid(column, row);
                if (gid == 0) {
                    continue;
                }
                if (!tileset.contains(gid)) {
                    const assets::TiledTileset* other = map.tilesetOf(gid);
                    return fail(ErrorCode::Unsupported, Module::Scene,
                                "Layer '{}': tile ({}, {}) is from tileset '{}', but a tilemap uses one "
                                "tileset ('{}')",
                                source.name, column, row, other != nullptr ? other->name : "?", tileset.name);
                }
                const IVec2 coord{static_cast<i32>(column), static_cast<i32>(source.size.y - 1 - row)};
                layer.setTile(coord, gid - tileset.firstGid + 1);
            }
        }
    }
    return tilemap;
}

Result<Tileset> tilesetFromTiled(const assets::TiledTileset& tileset, assets::AssetRegistry& registry,
                                 f32 pixelsPerUnit) {
    std::vector<TileProperties> properties;
    for (const assets::TiledTile& tile : tileset.tiles) {
        const std::optional<bool> solid = tile.properties.getBool("solid");
        if (!solid) {
            continue;
        }
        if (properties.size() <= tile.id) {
            properties.resize(static_cast<usize>(tile.id) + 1);
        }
        properties[tile.id].solid = *solid;
    }
    return Tileset::grid(registry.load<renderer::Texture>(tileset.image),
                         {.cellSize = tileset.tileSize,
                          .margin = UVec2{tileset.margin},
                          .spacing = UVec2{tileset.spacing},
                          .count = tileset.tileCount},
                         std::move(properties), pixelsPerUnit);
}

Aabb tiledObjectBounds(const assets::TiledMap& map, const assets::TiledObject& object,
                       f32 pixelsPerUnit) noexcept {
    const f32 mapHeight = static_cast<f32>(map.size.y) * static_cast<f32>(map.tileSize.y);
    const Vec2 min{object.position.x, mapHeight - object.position.y - object.size.y};
    const Vec2 max = min + object.size;
    return {.min = Vec3{min / pixelsPerUnit, 0.0F}, .max = Vec3{max / pixelsPerUnit, 0.0F}};
}

} // namespace teserra::scene
