#include <teserra/renderer/sprite_sheet.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace teserra::renderer {

namespace {

// Cells of `cell` texels with `spacing` between them that fit whole in `available` texels.
[[nodiscard]] u32 cellsThatFit(u32 available, u32 cell, u32 spacing) noexcept {
    if (available < cell) {
        return 0;
    }
    return (available - cell) / (cell + spacing) + 1;
}

} // namespace

UvRect regionUv(UVec2 textureSize, UVec2 position, UVec2 size) noexcept {
    TESERRA_ASSERT(textureSize.x != 0 && textureSize.y != 0, "regionUv() of a texture without texels");
    const Vec2 texels{textureSize};
    return {.min = Vec2{position} / texels, .max = Vec2{position + size} / texels};
}

SpriteSheet::SpriteSheet(assets::Asset<Texture> texture, std::vector<SpriteRegion> regions, f32 pixelsPerUnit)
    : m_texture(std::move(texture)), m_regions(std::move(regions)), m_pixelsPerUnit(pixelsPerUnit) {
    TESERRA_ASSERT(pixelsPerUnit > 0.0F, "A sprite sheet needs a positive pixelsPerUnit, not {}",
                   pixelsPerUnit);
}

Result<SpriteSheet> SpriteSheet::grid(assets::Asset<Texture> texture, const SpriteGrid& grid,
                                      f32 pixelsPerUnit) {
    if (grid.cellSize.x == 0 || grid.cellSize.y == 0) {
        return fail(ErrorCode::InvalidArgument, Module::Renderer,
                    "A sprite grid needs cells of at least 1x1 texels");
    }
    if (!(pixelsPerUnit > 0.0F)) {
        return fail(ErrorCode::InvalidArgument, Module::Renderer,
                    "A sprite sheet needs a positive pixelsPerUnit, not {}", pixelsPerUnit);
    }
    if (!texture.isReady()) {
        return fail(ErrorCode::NotFound, Module::Renderer, "The texture of the sprite grid is not loaded");
    }

    u32 columns = 0;
    u32 count = grid.count;
    if (texture.isFallback()) {
        // The real size is unknown: make the cells that were asked for, each showing the whole fallback.
        count = std::max(count, 1U);
        columns = count;
    } else {
        const UVec2 size = texture.get().size();
        const UVec2 available{size.x > grid.margin.x ? size.x - grid.margin.x : 0U,
                              size.y > grid.margin.y ? size.y - grid.margin.y : 0U};
        columns = cellsThatFit(available.x, grid.cellSize.x, grid.spacing.x);
        const u32 rows = cellsThatFit(available.y, grid.cellSize.y, grid.spacing.y);
        const u32 fitting = columns * rows;
        if (count > fitting) {
            return fail(ErrorCode::InvalidArgument, Module::Renderer,
                        "A {}x{} texture has room for {} cells of {}x{} texels, not {}", size.x, size.y,
                        fitting, grid.cellSize.x, grid.cellSize.y, count);
        }
        if (count == 0) {
            count = fitting;
        }
    }

    std::vector<SpriteRegion> regions;
    regions.reserve(count);
    for (u32 index = 0; index < count; ++index) {
        const UVec2 cell{index % columns, index / columns};
        regions.push_back({
            .name = std::format("{}{}", grid.namePrefix, index),
            .position = grid.margin + cell * (grid.cellSize + grid.spacing),
            .size = grid.cellSize,
        });
    }
    return SpriteSheet(std::move(texture), std::move(regions), pixelsPerUnit);
}

std::optional<usize> SpriteSheet::find(std::string_view name) const noexcept {
    const auto found = std::ranges::find(m_regions, name, &SpriteRegion::name);
    return found != m_regions.end() ? std::optional<usize>(static_cast<usize>(found - m_regions.begin()))
                                    : std::nullopt;
}

UvRect SpriteSheet::uv(usize region) const {
    TESERRA_ASSERT(region < m_regions.size(), "Sprite sheet region {} of {}", region, m_regions.size());
    if (!m_texture.isLoaded()) {
        return {};
    }
    const SpriteRegion& rect = m_regions[region];
    return regionUv(m_texture.get().size(), rect.position, rect.size);
}

Vec2 SpriteSheet::worldSize(usize region) const {
    TESERRA_ASSERT(region < m_regions.size(), "Sprite sheet region {} of {}", region, m_regions.size());
    return Vec2{m_regions[region].size} / m_pixelsPerUnit;
}

} // namespace teserra::renderer
