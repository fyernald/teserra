#pragma once

// Sprite sheets (docs/architecture/assets.md#sprite-sheets): named rectangular regions of one texture, with
// their size in world units from pixelsPerUnit (ADR-0024).
//
//   Asset<Texture> texture = assets.load<Texture>("assets://sprites/characters.png");
//   SpriteSheet sheet = *SpriteSheet::grid(texture, {.cellSize = {16, 16}, .namePrefix = "hero"});
//   const usize walk = *sheet.find("hero3");
//   sprite.uv = sheet.uv(walk);            // top-left origin
//   sprite.size = sheet.worldSize(walk);   // {1, 1} at 16 pixels per unit
//
// Sheets can also be files, loaded as assets (SpriteSheetLoader):
//
//   Asset<SpriteSheet> heroes = assets.load<SpriteSheet>("assets://sprites/characters.sheet.json");

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/frame_packet.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace teserra::renderer {

/// A named rectangle of a sprite sheet's texture, in texels with the origin at the top-left corner.
struct SpriteRegion {
    std::string name;
    /// Top-left texel.
    UVec2 position{0U};
    /// Width and height in texels.
    UVec2 size{0U};

    friend bool operator==(const SpriteRegion&, const SpriteRegion&) = default;
};

/// The UVs of the `size` texels at `position` in a texture of `textureSize` texels, top-left origin
/// (ADR-0024). The texture size must not be zero.
[[nodiscard]] UvRect regionUv(UVec2 textureSize, UVec2 position, UVec2 size) noexcept;

/// Cells of equal size, laid out in rows from the top left of the texture.
struct SpriteGrid {
    /// Width and height of a cell in texels; not zero.
    UVec2 cellSize{0U};
    /// Texels before the first cell, from the left and top edges.
    UVec2 margin{0U};
    /// Texels between neighboring cells, horizontally and vertically.
    UVec2 spacing{0U};
    /// Number of cells, row by row; 0 takes every cell that fits whole.
    u32 count = 0;
    /// Cells are named "<namePrefix><index>", counting from 0: "hero0", "hero1", ...
    std::string_view namePrefix{};
};

/// Default texels per world unit: 16-pixel art fills one unit.
inline constexpr f32 kDefaultPixelsPerUnit = 16.0F;

/// Named regions of one texture. Region indices are stable: they follow the order of the regions given.
/// The sheet holds its texture: while it lives, the texture stays loaded. Region rectangles are texels of the
/// texture the sheet was made for; when the texture is a fallback (its file is missing), every region shows
/// the whole fallback.
class SpriteSheet {
public:
    /// A sheet of `regions` of `texture`. `pixelsPerUnit` must be positive.
    SpriteSheet(assets::Asset<Texture> texture, std::vector<SpriteRegion> regions,
                f32 pixelsPerUnit = kDefaultPixelsPerUnit);

    /// A sheet of the cells of `grid`, which needs a ready texture to know its size.
    ///
    /// Errors: InvalidArgument (a zero cell size, a `pixelsPerUnit` that is not positive, or more cells
    /// asked for than fit), NotFound (the texture is not ready: null or failed without a fallback). A
    /// fallback texture gives the asked-for `count` cells (at least one), each showing the whole fallback.
    [[nodiscard]] static Result<SpriteSheet> grid(assets::Asset<Texture> texture, const SpriteGrid& grid,
                                                  f32 pixelsPerUnit = kDefaultPixelsPerUnit);

    /// The texture the regions cut, kept loaded by the sheet.
    [[nodiscard]] const assets::Asset<Texture>& texture() const noexcept { return m_texture; }
    [[nodiscard]] f32 pixelsPerUnit() const noexcept { return m_pixelsPerUnit; }
    /// The regions, in the order they were defined.
    [[nodiscard]] std::span<const SpriteRegion> regions() const noexcept { return m_regions; }

    /// The index of the first region named `name`, or std::nullopt.
    [[nodiscard]] std::optional<usize> find(std::string_view name) const noexcept;

    /// The UVs of region `region` (asserted to exist); the whole texture when it is a fallback or not ready.
    [[nodiscard]] UvRect uv(usize region) const;

    /// The size of region `region` in world units: its texels divided by pixelsPerUnit().
    [[nodiscard]] Vec2 worldSize(usize region) const;

private:
    assets::Asset<Texture> m_texture;
    std::vector<SpriteRegion> m_regions;
    f32 m_pixelsPerUnit;
};

/// Loads SpriteSheets from JSON files (by convention `<name>.sheet.json`): the texture, loaded as a
/// dependency, and either a grid or a list of named regions, in texels from the top-left corner.
///
///     {"texture": "characters.png", "pixelsPerUnit": 16,
///      "grid": {"cellSize": [16, 16], "margin": [0, 0], "spacing": [0, 0], "count": 0, "namePrefix":
///      "hero"}}
///
///     {"texture": "characters.png",
///      "regions": [{"name": "idle", "position": [0, 0], "size": [16, 32]}]}
///
/// "texture" is a virtual path or a path relative to the sheet file. "pixelsPerUnit" (default 16), and the
/// grid's "margin", "spacing", "count" and "namePrefix" are optional. A missing texture gives its fallback,
/// as in code. Errors (InvalidData) name the field: malformed JSON, missing or mistyped fields, both or
/// neither of "grid" and "regions", and regions outside the texture.
class SpriteSheetLoader final : public assets::AssetLoader<SpriteSheet> {
public:
    /// {"json"}: asset types are part of the key, so other JSON assets have their own loaders.
    [[nodiscard]] std::span<const std::string_view> extensions() const override { return kExtensions; }
    [[nodiscard]] Result<SpriteSheet> load(assets::AssetLoadContext& context,
                                           std::span<const std::byte> bytes) override;

private:
    static constexpr std::array<std::string_view, 1> kExtensions{"json"};
};

} // namespace teserra::renderer
