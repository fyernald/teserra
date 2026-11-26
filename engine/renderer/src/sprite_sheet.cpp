#include <teserra/renderer/sprite_sheet.hpp>

#include <teserra/assets/json.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/assert.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace teserra::renderer {

namespace {

Unexpected invalidSheet(std::string_view message) {
    return Unexpected(
        Error{.code = ErrorCode::InvalidData, .module = Module::Renderer, .message = std::string(message)});
}

// A [x, y] pair of non-negative integers; `fallback` if it is missing and optional.
Result<UVec2> readPair(assets::JsonValue object, std::string_view key, std::string_view context,
                       std::optional<UVec2> fallback = std::nullopt) {
    const assets::JsonValue value = object[key];
    if (!value.exists() && fallback) {
        return *fallback;
    }
    std::array<i64, 2> numbers{-1, -1};
    usize index = 0;
    value.forEach([&](assets::JsonValue element) {
        if (index < 2) {
            numbers[index] = element.asInt().value_or(-1);
        }
        ++index;
    });
    constexpr auto kMax = static_cast<i64>(1U << 20U);
    if (!value.isArray() || index != 2 || numbers[0] < 0 || numbers[1] < 0 || numbers[0] > kMax ||
        numbers[1] > kMax) {
        return invalidSheet(
            std::format("{}'{}' must be an array of two non-negative integers", context, key));
    }
    return UVec2{static_cast<u32>(numbers[0]), static_cast<u32>(numbers[1])};
}

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

Result<SpriteSheet> SpriteSheetLoader::load(assets::AssetLoadContext& context,
                                            std::span<const std::byte> bytes) {
    Result<assets::JsonDocument> document = assets::JsonDocument::parse(bytes);
    if (!document) {
        return Unexpected(Error{
            .code = document.error().code, .module = Module::Renderer, .message = document.error().message});
    }
    const assets::JsonValue root = document->root();
    if (!root.isObject()) {
        return invalidSheet(std::format("A sprite sheet file holds an object, not {}", root.typeName()));
    }

    const std::optional<std::string_view> texturePath = root["texture"].asString();
    if (!texturePath) {
        return invalidSheet(std::format("'texture' must be a string, not {}", root["texture"].typeName()));
    }
    f32 pixelsPerUnit = kDefaultPixelsPerUnit;
    if (root["pixelsPerUnit"].exists()) {
        const std::optional<f64> value = root["pixelsPerUnit"].asNumber();
        if (!value || !(*value > 0.0)) {
            return invalidSheet("'pixelsPerUnit' must be a positive number");
        }
        pixelsPerUnit = static_cast<f32>(*value);
    }
    const assets::JsonValue grid = root["grid"];
    const assets::JsonValue regions = root["regions"];
    if (grid.exists() == regions.exists()) {
        return invalidSheet("A sprite sheet has either 'grid' or 'regions'");
    }

    TESERRA_TRY_ASSIGN(const std::string resolved, assets::resolveRelativePath(context.path(), *texturePath));
    assets::Asset<Texture> texture = context.load<Texture>(resolved);

    if (grid.exists()) {
        if (!grid.isObject()) {
            return invalidSheet(std::format("'grid' must be an object, not {}", grid.typeName()));
        }
        TESERRA_TRY_ASSIGN(const UVec2 cellSize, readPair(grid, "cellSize", "grid: "));
        TESERRA_TRY_ASSIGN(const UVec2 margin, readPair(grid, "margin", "grid: ", UVec2{0U}));
        TESERRA_TRY_ASSIGN(const UVec2 spacing, readPair(grid, "spacing", "grid: ", UVec2{0U}));
        const std::optional<i64> count =
            grid["count"].exists() ? grid["count"].asInt() : std::optional<i64>(0);
        if (!count || *count < 0 || *count > std::numeric_limits<i32>::max()) {
            return invalidSheet("grid: 'count' must be a non-negative integer");
        }
        const std::optional<std::string_view> prefix =
            grid["namePrefix"].exists() ? grid["namePrefix"].asString() : std::optional<std::string_view>("");
        if (!prefix) {
            return invalidSheet("grid: 'namePrefix' must be a string");
        }
        Result<SpriteSheet> sheet = SpriteSheet::grid(std::move(texture),
                                                      {.cellSize = cellSize,
                                                       .margin = margin,
                                                       .spacing = spacing,
                                                       .count = static_cast<u32>(*count),
                                                       .namePrefix = *prefix},
                                                      pixelsPerUnit);
        if (!sheet && sheet.error().code == ErrorCode::InvalidArgument) {
            return invalidSheet(std::format("grid: {}", sheet.error().message));
        }
        return sheet;
    }

    if (!regions.isArray()) {
        return invalidSheet(std::format("'regions' must be an array, not {}", regions.typeName()));
    }
    // Regions are checked against the texture they were made for; a fallback has another size.
    const std::optional<UVec2> textureSize =
        texture.isLoaded() ? std::optional<UVec2>(texture.get().size()) : std::nullopt;
    std::vector<SpriteRegion> list;
    list.reserve(regions.size());
    Result<void> result;
    regions.forEach([&](assets::JsonValue item) {
        const std::string context = std::format("region {}: ", list.size());
        const std::optional<std::string_view> name = item["name"].asString();
        if (!name) {
            result = invalidSheet(std::format("{}'name' must be a string", context));
            return false;
        }
        Result<UVec2> position = readPair(item, "position", context);
        Result<UVec2> size = position ? readPair(item, "size", context) : Result<UVec2>(UVec2{0U});
        if (!position || !size) {
            result = Unexpected(position ? size.error() : position.error());
            return false;
        }
        if (size->x == 0 || size->y == 0) {
            result = invalidSheet(std::format("region '{}' has no texels", *name));
            return false;
        }
        if (textureSize &&
            (position->x + size->x > textureSize->x || position->y + size->y > textureSize->y)) {
            result = invalidSheet(std::format("region '{}' ({}, {}; {}x{}) is outside the {}x{} texture",
                                              *name, position->x, position->y, size->x, size->y,
                                              textureSize->x, textureSize->y));
            return false;
        }
        list.push_back({.name = std::string(*name), .position = *position, .size = *size});
        return true;
    });
    TESERRA_TRY(result);
    return SpriteSheet(std::move(texture), std::move(list), pixelsPerUnit);
}

} // namespace teserra::renderer
