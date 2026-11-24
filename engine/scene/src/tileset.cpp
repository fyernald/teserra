#include <teserra/scene/tileset.hpp>

#include <teserra/core/assert.hpp>

#include <utility>

namespace teserra::scene {

namespace {

const TileProperties kDefaultProperties{};

} // namespace

Tileset::Tileset(renderer::SpriteSheet sheet, std::vector<TileProperties> properties)
    : m_sheet(std::move(sheet)), m_properties(std::move(properties)) {
    TESERRA_ASSERT(m_properties.size() <= tileCount(), "A tileset of {} tiles got properties for {}",
                   tileCount(), m_properties.size());
}

Result<Tileset> Tileset::grid(assets::Asset<renderer::Texture> texture, const renderer::SpriteGrid& grid,
                              std::vector<TileProperties> properties, f32 pixelsPerUnit) {
    TESERRA_TRY_ASSIGN(renderer::SpriteSheet sheet,
                       renderer::SpriteSheet::grid(std::move(texture), grid, pixelsPerUnit));
    if (properties.size() > sheet.regions().size()) {
        return fail(ErrorCode::InvalidArgument, Module::Scene, "A tileset of {} tiles got properties for {}",
                    sheet.regions().size(), properties.size());
    }
    return Tileset(std::move(sheet), std::move(properties));
}

renderer::UvRect Tileset::uv(TileId tile) const {
    TESERRA_ASSERT(contains(tile), "Tile {} is not in a tileset of {} tiles", tile, tileCount());
    return m_sheet.uv(tile - 1);
}

const TileProperties& Tileset::properties(TileId tile) const noexcept {
    if (tile == kNoTile || tile > m_properties.size()) {
        return kDefaultProperties;
    }
    return m_properties[tile - 1];
}

} // namespace teserra::scene
