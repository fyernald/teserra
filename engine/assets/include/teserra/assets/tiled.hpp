#pragma once

// Tiled maps (docs/architecture/assets.md#tiled-maps): orthogonal maps saved by the Tiled editor in its JSON
// format (.tmj), with embedded or external (.tsj) tilesets, tile layers, rectangle objects and custom
// properties. This is the map as Tiled describes it, in pixels with y down; the scene layer turns it into a
// Tilemap and a Tileset (<teserra/scene/tiled.hpp>).
//
//   registry.registerLoader<TiledMap>(std::make_unique<TiledMapLoader>());
//   Asset<TiledMap> level = registry.load<TiledMap>("assets://maps/level1.tmj");
//
// Unsupported features fail the load with an Error that names them: infinite maps, other orientations,
// compressed or base64 layer data, flipped tiles, group and image layers, and objects that are not
// rectangles.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace teserra::assets {

/// A custom property. Tiled's types map to: bool; int and object (an object id) to i64; float to f64;
/// string, color ("#AARRGGBB") and file (a virtual path, resolved like images) to std::string.
struct TiledProperty {
    std::string name{};
    std::variant<bool, i64, f64, std::string> value{};

    friend bool operator==(const TiledProperty&, const TiledProperty&) = default;
};

/// The custom properties of a map, layer, object, tileset or tile, in the file's order.
struct TiledProperties {
    std::vector<TiledProperty> items{};

    /// The property named `name`, or nullptr.
    [[nodiscard]] const TiledProperty* find(std::string_view name) const noexcept;
    /// The value of a bool property named `name`; std::nullopt if there is none or it is not a bool.
    [[nodiscard]] std::optional<bool> getBool(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<i64> getInt(std::string_view name) const noexcept;
    /// Floats, and ints converted.
    [[nodiscard]] std::optional<f64> getNumber(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::string_view> getString(std::string_view name) const noexcept;
};

/// A tile of a tileset with custom properties (Tiled lists only those).
struct TiledTile {
    /// The tile's index in its tileset, from 0.
    u32 id = 0;
    TiledProperties properties{};
};

/// A tileset cut from one image in a grid.
struct TiledTileset {
    /// The global tile id (gid) of the tileset's first tile; layer data uses gids.
    u32 firstGid = 1;
    std::string name{};
    /// The image's virtual path, resolved against the file that names it (resolveRelativePath()):
    /// "assets://tiles/terrain.png".
    std::string image{};
    UVec2 imageSize{0U};
    UVec2 tileSize{0U};
    /// Pixels around the grid and between its tiles.
    u32 margin = 0;
    u32 spacing = 0;
    u32 columns = 0;
    u32 tileCount = 0;
    /// The tiles with properties, by id.
    std::vector<TiledTile> tiles{};
    TiledProperties properties{};

    /// Whether `gid` is one of this tileset's tiles.
    [[nodiscard]] bool contains(u32 gid) const noexcept {
        return gid >= firstGid && gid - firstGid < tileCount;
    }
    /// The properties of tile `id` (from 0); nullptr if it has none.
    [[nodiscard]] const TiledProperties* tileProperties(u32 id) const noexcept;
};

/// A layer of tiles: width x height gids, row by row from the top row (Tiled's order), 0 for no tile.
struct TiledTileLayer {
    std::string name{};
    UVec2 size{0U};
    std::vector<u32> gids{};
    bool visible = true;
    f32 opacity = 1.0F;
    /// The layer's offset in pixels.
    Vec2 offset{0.0F};
    TiledProperties properties{};

    /// The gid at column `x`, row `y` (row 0 at the top).
    [[nodiscard]] u32 gid(u32 x, u32 y) const noexcept { return gids[(static_cast<usize>(y) * size.x) + x]; }
};

/// A rectangle object, in pixels with y down from the map's top-left corner.
struct TiledObject {
    u32 id = 0;
    std::string name{};
    /// The object's class (Tiled 1.9+) or type.
    std::string type{};
    /// The rectangle's top-left corner.
    Vec2 position{0.0F};
    Vec2 size{0.0F};
    /// Degrees, clockwise.
    f32 rotation = 0.0F;
    bool visible = true;
    TiledProperties properties{};
};

/// An object layer of a Tiled map: named points and shapes, such as spawn points.
struct TiledObjectLayer {
    std::string name{};
    std::vector<TiledObject> objects{};
    bool visible = true;
    TiledProperties properties{};
};

/// An orthogonal, finite Tiled map.
struct TiledMap {
    /// Width and height in tiles.
    UVec2 size{0U};
    /// Width and height of a tile in pixels.
    UVec2 tileSize{0U};
    std::vector<TiledTileset> tilesets{};
    /// In the file's order, bottom layer first.
    std::vector<TiledTileLayer> tileLayers{};
    std::vector<TiledObjectLayer> objectLayers{};
    TiledProperties properties{};

    /// The tileset `gid` belongs to; nullptr for 0 and for gids of no tileset.
    [[nodiscard]] const TiledTileset* tilesetOf(u32 gid) const noexcept;
    [[nodiscard]] const TiledTileLayer* findTileLayer(std::string_view name) const noexcept;
    [[nodiscard]] const TiledObjectLayer* findObjectLayer(std::string_view name) const noexcept;
};

/// Reads the bytes of another file by virtual path: external tilesets.
using TiledFileReader = std::function<Result<std::vector<std::byte>>(std::string_view virtualPath)>;

/// Parses the .tmj file at virtual path `mapPath` (relative paths in it are resolved against it), reading
/// external tilesets through `readFile`. Errors are InvalidData (malformed JSON, missing or wrong fields,
/// gids of no tileset) or Unsupported (features listed at the top of this header), with the layer and tile
/// or object they are about; errors of a tileset name its file.
[[nodiscard]] Result<TiledMap> parseTiledMap(std::span<const std::byte> bytes, std::string_view mapPath,
                                             const TiledFileReader& readFile);

/// Loads TiledMaps from .tmj files; external tilesets are read through the context's mount points.
class TiledMapLoader final : public AssetLoader<TiledMap> {
public:
    /// {"tmj"}
    [[nodiscard]] std::span<const std::string_view> extensions() const override { return kExtensions; }
    [[nodiscard]] Result<TiledMap> load(AssetLoadContext& context, std::span<const std::byte> bytes) override;

private:
    static constexpr std::array<std::string_view, 1> kExtensions{"tmj"};
};

} // namespace teserra::assets
