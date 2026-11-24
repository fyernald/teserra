#include <teserra/assets/tiled.hpp>

#include <teserra/assets/json.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace teserra::assets {

namespace {

// Tiled stores flips and rotations in the top bits of a gid.
constexpr u32 kGidFlagMask = 0xF0000000U;

Error invalid(std::string_view context, std::string_view message) {
    return Error{.code = ErrorCode::InvalidData,
                 .module = Module::Assets,
                 .message = std::format("{}: {}", context, message)};
}

Error unsupported(std::string_view context, std::string_view message) {
    return Error{.code = ErrorCode::Unsupported,
                 .module = Module::Assets,
                 .message = std::format("{}: {}", context, message)};
}

// An integer member in [min, max]; `fallback` when it is missing and optional.
Result<i64> readInt(JsonValue object, std::string_view key, std::string_view context, i64 min, i64 max,
                    std::optional<i64> fallback = std::nullopt) {
    const JsonValue value = object[key];
    if (!value.exists() && fallback) {
        return *fallback;
    }
    const std::optional<i64> number = value.asInt();
    if (!number) {
        return Unexpected(
            invalid(context, std::format("'{}' must be an integer, not {}", key, value.typeName())));
    }
    if (*number < min || *number > max) {
        return Unexpected(
            invalid(context, std::format("'{}' is {}, outside [{}, {}]", key, *number, min, max)));
    }
    return *number;
}

Result<u32> readCount(JsonValue object, std::string_view key, std::string_view context, i64 min,
                      std::optional<i64> fallback = std::nullopt) {
    TESERRA_TRY_ASSIGN(const i64 value,
                       readInt(object, key, context, min, std::numeric_limits<i32>::max(), fallback));
    return static_cast<u32>(value);
}

Result<f32> readNumber(JsonValue object, std::string_view key, std::string_view context, f32 fallback) {
    const JsonValue value = object[key];
    if (!value.exists()) {
        return fallback;
    }
    const std::optional<f64> number = value.asNumber();
    if (!number) {
        return Unexpected(
            invalid(context, std::format("'{}' must be a number, not {}", key, value.typeName())));
    }
    return static_cast<f32>(*number);
}

Result<std::string> readString(JsonValue object, std::string_view key, std::string_view context,
                               std::optional<std::string_view> fallback = std::nullopt) {
    const JsonValue value = object[key];
    if (!value.exists() && fallback) {
        return std::string(*fallback);
    }
    const std::optional<std::string_view> text = value.asString();
    if (!text) {
        return Unexpected(
            invalid(context, std::format("'{}' must be a string, not {}", key, value.typeName())));
    }
    return std::string(*text);
}

Result<bool> readBool(JsonValue object, std::string_view key, std::string_view context, bool fallback) {
    const JsonValue value = object[key];
    if (!value.exists()) {
        return fallback;
    }
    const std::optional<bool> flag = value.asBool();
    if (!flag) {
        return Unexpected(
            invalid(context, std::format("'{}' must be a boolean, not {}", key, value.typeName())));
    }
    return *flag;
}

// Visits the elements of an optional array member, stopping at the first error.
template <typename F>
Result<void> forEachIn(JsonValue object, std::string_view key, std::string_view context, F&& visit) {
    const JsonValue array = object[key];
    if (!array.exists()) {
        return {};
    }
    if (!array.isArray()) {
        return Unexpected(
            invalid(context, std::format("'{}' must be an array, not {}", key, array.typeName())));
    }
    Result<void> result;
    array.forEach([&](JsonValue element) {
        result = visit(element);
        return result.has_value();
    });
    return result;
}

// `filePath` is the file the properties are in, for "file" properties.
Result<TiledProperties> readProperties(JsonValue object, std::string_view context,
                                       std::string_view filePath) {
    TiledProperties properties;
    const Result<void> visited =
        forEachIn(object, "properties", context, [&](JsonValue item) -> Result<void> {
            TESERRA_TRY_ASSIGN(std::string name, readString(item, "name", context));
            const std::string where = std::format("{}: property '{}'", context, name);
            TESERRA_TRY_ASSIGN(const std::string type, readString(item, "type", where, "string"));
            const JsonValue value = item["value"];
            TiledProperty property{.name = std::move(name), .value = false};
            if (type == "bool") {
                TESERRA_TRY_ASSIGN(property.value, readBool(item, "value", where, false));
            } else if (type == "int" || type == "object") {
                TESERRA_TRY_ASSIGN(property.value,
                                   readInt(item, "value", where, std::numeric_limits<i64>::min(),
                                           std::numeric_limits<i64>::max(), 0));
            } else if (type == "float") {
                const std::optional<f64> number = value.exists() ? value.asNumber() : std::optional<f64>(0.0);
                if (!number) {
                    return Unexpected(
                        invalid(where, std::format("'value' must be a number, not {}", value.typeName())));
                }
                property.value = *number;
            } else if (type == "string" || type == "color") {
                TESERRA_TRY_ASSIGN(property.value, readString(item, "value", where, ""));
            } else if (type == "file") {
                TESERRA_TRY_ASSIGN(const std::string file, readString(item, "value", where, ""));
                if (file.empty()) {
                    property.value = file;
                } else {
                    TESERRA_TRY_ASSIGN(property.value, resolveRelativePath(filePath, file));
                }
            } else {
                return Unexpected(
                    unsupported(where, std::format("properties of type '{}' are not supported", type)));
            }
            properties.items.push_back(std::move(property));
            return {};
        });
    TESERRA_TRY(visited);
    return properties;
}

// The tileset fields shared by embedded tilesets and .tsj files; `filePath` is the file they are in.
Result<void> readTilesetFields(JsonValue object, std::string_view filePath, std::string_view context,
                               TiledTileset& tileset) {
    TESERRA_TRY_ASSIGN(tileset.name, readString(object, "name", context, ""));
    const std::string named =
        tileset.name.empty() ? std::string(context) : std::format("{} '{}'", context, tileset.name);
    if (!object["image"].exists()) {
        return Unexpected(unsupported(named, "tilesets of separate images are not supported; use one image"));
    }
    TESERRA_TRY_ASSIGN(const std::string image, readString(object, "image", named));
    TESERRA_TRY_ASSIGN(tileset.image, resolveRelativePath(filePath, image));
    TESERRA_TRY_ASSIGN(tileset.imageSize.x, readCount(object, "imagewidth", named, 1));
    TESERRA_TRY_ASSIGN(tileset.imageSize.y, readCount(object, "imageheight", named, 1));
    TESERRA_TRY_ASSIGN(tileset.tileSize.x, readCount(object, "tilewidth", named, 1));
    TESERRA_TRY_ASSIGN(tileset.tileSize.y, readCount(object, "tileheight", named, 1));
    TESERRA_TRY_ASSIGN(tileset.margin, readCount(object, "margin", named, 0, 0));
    TESERRA_TRY_ASSIGN(tileset.spacing, readCount(object, "spacing", named, 0, 0));
    TESERRA_TRY_ASSIGN(tileset.columns, readCount(object, "columns", named, 1));
    TESERRA_TRY_ASSIGN(tileset.tileCount, readCount(object, "tilecount", named, 1));
    TESERRA_TRY_ASSIGN(tileset.properties, readProperties(object, named, filePath));
    return forEachIn(object, "tiles", named, [&](JsonValue item) -> Result<void> {
        TESERRA_TRY_ASSIGN(const u32 id, readCount(item, "id", named, 0));
        const std::string tileContext = std::format("{}: tile {}", named, id);
        if (id >= tileset.tileCount) {
            return Unexpected(
                invalid(tileContext, std::format("the tileset has {} tiles", tileset.tileCount)));
        }
        if (item["animation"].exists()) {
            return Unexpected(unsupported(tileContext, "animated tiles are not supported"));
        }
        TESERRA_TRY_ASSIGN(TiledProperties properties, readProperties(item, tileContext, filePath));
        if (!properties.items.empty()) {
            tileset.tiles.push_back({.id = id, .properties = std::move(properties)});
        }
        return {};
    });
}

Result<TiledTileset> readTileset(JsonValue entry, std::string_view mapPath, usize index,
                                 const TiledFileReader& readFile) {
    const std::string context = std::format("tileset {}", index);
    TiledTileset tileset;
    TESERRA_TRY_ASSIGN(tileset.firstGid, readCount(entry, "firstgid", context, 1));
    if (!entry["source"].exists()) {
        TESERRA_TRY(readTilesetFields(entry, mapPath, context, tileset));
        return tileset;
    }
    TESERRA_TRY_ASSIGN(const std::string source, readString(entry, "source", context));
    TESERRA_TRY_ASSIGN(const std::string path, resolveRelativePath(mapPath, source));
    const std::string fileContext = std::format("tileset '{}'", path);
    if (!path.ends_with(".tsj") && !path.ends_with(".json")) {
        return Unexpected(unsupported(fileContext, "only JSON tilesets (.tsj) are supported, not TSX"));
    }
    if (!readFile) {
        return Unexpected(unsupported(fileContext, "external tilesets need a file reader"));
    }
    Result<std::vector<std::byte>> bytes = readFile(path);
    if (!bytes) {
        return Unexpected(Error{.code = bytes.error().code,
                                .module = Module::Assets,
                                .message = std::format("{}: {}", fileContext, bytes.error().message)});
    }
    Result<JsonDocument> document = JsonDocument::parse(*bytes);
    if (!document) {
        return Unexpected(invalid(fileContext, document.error().message));
    }
    TESERRA_TRY(readTilesetFields(document->root(), path, fileContext, tileset));
    return tileset;
}

Result<TiledTileLayer> readTileLayer(JsonValue object, const TiledMap& map, std::string_view mapPath) {
    TiledTileLayer layer;
    TESERRA_TRY_ASSIGN(layer.name, readString(object, "name", "a tile layer", ""));
    const std::string context = std::format("layer '{}'", layer.name);
    TESERRA_TRY_ASSIGN(layer.size.x, readCount(object, "width", context, 1));
    TESERRA_TRY_ASSIGN(layer.size.y, readCount(object, "height", context, 1));
    TESERRA_TRY_ASSIGN(layer.visible, readBool(object, "visible", context, true));
    TESERRA_TRY_ASSIGN(layer.opacity, readNumber(object, "opacity", context, 1.0F));
    TESERRA_TRY_ASSIGN(layer.offset.x, readNumber(object, "offsetx", context, 0.0F));
    TESERRA_TRY_ASSIGN(layer.offset.y, readNumber(object, "offsety", context, 0.0F));
    TESERRA_TRY_ASSIGN(layer.properties, readProperties(object, context, mapPath));

    TESERRA_TRY_ASSIGN(const std::string encoding, readString(object, "encoding", context, "csv"));
    TESERRA_TRY_ASSIGN(const std::string compression, readString(object, "compression", context, ""));
    if (encoding != "csv" || !compression.empty()) {
        return Unexpected(
            unsupported(context, std::format("'{}' layer data{} is not supported; save tile layers "
                                             "in the CSV format",
                                             encoding, compression.empty() ? "" : " with compression")));
    }
    const JsonValue data = object["data"];
    if (!data.isArray()) {
        return Unexpected(invalid(context, std::format("'data' must be an array, not {}", data.typeName())));
    }
    const usize expected = static_cast<usize>(layer.size.x) * layer.size.y;
    if (data.size() != expected) {
        return Unexpected(invalid(context, std::format("'data' has {} tiles, not {} ({}x{})", data.size(),
                                                       expected, layer.size.x, layer.size.y)));
    }
    layer.gids.reserve(expected);
    Result<void> result;
    data.forEach([&](JsonValue element) {
        const u32 column = static_cast<u32>(layer.gids.size() % layer.size.x);
        const u32 row = static_cast<u32>(layer.gids.size() / layer.size.x);
        const std::optional<i64> gid = element.asInt();
        if (!gid || *gid < 0 || *gid > std::numeric_limits<u32>::max()) {
            result = Unexpected(invalid(context, std::format("tile ({}, {}) is not a tile id", column, row)));
            return false;
        }
        const auto value = static_cast<u32>(*gid);
        if ((value & kGidFlagMask) != 0) {
            result = Unexpected(unsupported(
                context,
                std::format("tile ({}, {}) is flipped or rotated, which is not supported", column, row)));
            return false;
        }
        if (value != 0 && map.tilesetOf(value) == nullptr) {
            result = Unexpected(invalid(
                context, std::format("tile ({}, {}) has id {}, which is in no tileset", column, row, value)));
            return false;
        }
        layer.gids.push_back(value);
        return true;
    });
    TESERRA_TRY(result);
    return layer;
}

Result<TiledObjectLayer> readObjectLayer(JsonValue object, std::string_view mapPath) {
    TiledObjectLayer layer;
    TESERRA_TRY_ASSIGN(layer.name, readString(object, "name", "an object layer", ""));
    const std::string context = std::format("layer '{}'", layer.name);
    TESERRA_TRY_ASSIGN(layer.visible, readBool(object, "visible", context, true));
    TESERRA_TRY_ASSIGN(layer.properties, readProperties(object, context, mapPath));
    const Result<void> visited = forEachIn(object, "objects", context, [&](JsonValue item) -> Result<void> {
        TiledObject result;
        TESERRA_TRY_ASSIGN(result.id, readCount(item, "id", context, 0, 0));
        TESERRA_TRY_ASSIGN(result.name, readString(item, "name", context, ""));
        const std::string where = std::format("{}: object {} '{}'", context, result.id, result.name);
        for (const std::string_view shape : {"ellipse", "point", "polygon", "polyline", "text", "gid"}) {
            if (item[shape].exists() && item[shape].asBool() != false) {
                return Unexpected(
                    unsupported(where, std::format("{} objects are not supported, only rectangles",
                                                   shape == "gid" ? "tile" : shape)));
            }
        }
        // Tiled 1.9 renamed "type" to "class".
        TESERRA_TRY_ASSIGN(result.type,
                           readString(item, item["class"].exists() ? "class" : "type", where, ""));
        TESERRA_TRY_ASSIGN(result.position.x, readNumber(item, "x", where, 0.0F));
        TESERRA_TRY_ASSIGN(result.position.y, readNumber(item, "y", where, 0.0F));
        TESERRA_TRY_ASSIGN(result.size.x, readNumber(item, "width", where, 0.0F));
        TESERRA_TRY_ASSIGN(result.size.y, readNumber(item, "height", where, 0.0F));
        TESERRA_TRY_ASSIGN(result.rotation, readNumber(item, "rotation", where, 0.0F));
        TESERRA_TRY_ASSIGN(result.visible, readBool(item, "visible", where, true));
        TESERRA_TRY_ASSIGN(result.properties, readProperties(item, where, mapPath));
        layer.objects.push_back(std::move(result));
        return {};
    });
    TESERRA_TRY(visited);
    return layer;
}

Result<TiledMap> readMap(JsonValue root, std::string_view mapPath, const TiledFileReader& readFile) {
    constexpr std::string_view kMap = "map";
    if (!root.isObject()) {
        return Unexpected(invalid(kMap, std::format("the file holds {}, not an object", root.typeName())));
    }
    TESERRA_TRY_ASSIGN(const std::string orientation, readString(root, "orientation", kMap, "orthogonal"));
    if (orientation != "orthogonal") {
        return Unexpected(
            unsupported(kMap, std::format("{} maps are not supported, only orthogonal ones", orientation)));
    }
    TESERRA_TRY_ASSIGN(const bool infinite, readBool(root, "infinite", kMap, false));
    if (infinite) {
        return Unexpected(
            unsupported(kMap, "infinite maps are not supported; turn off 'Infinite' in the map properties"));
    }

    TiledMap map;
    TESERRA_TRY_ASSIGN(map.size.x, readCount(root, "width", kMap, 1));
    TESERRA_TRY_ASSIGN(map.size.y, readCount(root, "height", kMap, 1));
    TESERRA_TRY_ASSIGN(map.tileSize.x, readCount(root, "tilewidth", kMap, 1));
    TESERRA_TRY_ASSIGN(map.tileSize.y, readCount(root, "tileheight", kMap, 1));
    TESERRA_TRY_ASSIGN(map.properties, readProperties(root, kMap, mapPath));

    const Result<void> visited = forEachIn(root, "tilesets", kMap, [&](JsonValue entry) -> Result<void> {
        TESERRA_TRY_ASSIGN(TiledTileset tileset, readTileset(entry, mapPath, map.tilesets.size(), readFile));
        map.tilesets.push_back(std::move(tileset));
        return {};
    });
    TESERRA_TRY(visited);
    std::ranges::sort(map.tilesets, {}, &TiledTileset::firstGid);

    const Result<void> layersRead = forEachIn(root, "layers", kMap, [&](JsonValue layer) -> Result<void> {
        TESERRA_TRY_ASSIGN(const std::string type, readString(layer, "type", "a layer"));
        if (type == "tilelayer") {
            TESERRA_TRY_ASSIGN(TiledTileLayer tiles, readTileLayer(layer, map, mapPath));
            map.tileLayers.push_back(std::move(tiles));
            return {};
        }
        if (type == "objectgroup") {
            TESERRA_TRY_ASSIGN(TiledObjectLayer objects, readObjectLayer(layer, mapPath));
            map.objectLayers.push_back(std::move(objects));
            return {};
        }
        std::string_view kind = type;
        if (type == "imagelayer") {
            kind = "image";
        }
        return Unexpected(unsupported(std::format("layer '{}'", layer["name"].asString().value_or("")),
                                      std::format("{} layers are not supported", kind)));
    });
    TESERRA_TRY(layersRead);
    return map;
}

} // namespace

const TiledProperty* TiledProperties::find(std::string_view name) const noexcept {
    const auto it = std::ranges::find(items, name, &TiledProperty::name);
    return it != items.end() ? &*it : nullptr;
}

std::optional<bool> TiledProperties::getBool(std::string_view name) const noexcept {
    const TiledProperty* property = find(name);
    const bool* value = property != nullptr ? std::get_if<bool>(&property->value) : nullptr;
    return value != nullptr ? std::optional<bool>(*value) : std::nullopt;
}

std::optional<i64> TiledProperties::getInt(std::string_view name) const noexcept {
    const TiledProperty* property = find(name);
    const i64* value = property != nullptr ? std::get_if<i64>(&property->value) : nullptr;
    return value != nullptr ? std::optional<i64>(*value) : std::nullopt;
}

std::optional<f64> TiledProperties::getNumber(std::string_view name) const noexcept {
    const TiledProperty* property = find(name);
    if (property == nullptr) {
        return std::nullopt;
    }
    if (const f64* real = std::get_if<f64>(&property->value)) {
        return *real;
    }
    if (const i64* integer = std::get_if<i64>(&property->value)) {
        return static_cast<f64>(*integer);
    }
    return std::nullopt;
}

std::optional<std::string_view> TiledProperties::getString(std::string_view name) const noexcept {
    const TiledProperty* property = find(name);
    const std::string* value = property != nullptr ? std::get_if<std::string>(&property->value) : nullptr;
    return value != nullptr ? std::optional<std::string_view>(*value) : std::nullopt;
}

const TiledProperties* TiledTileset::tileProperties(u32 id) const noexcept {
    const auto it = std::ranges::find(tiles, id, &TiledTile::id);
    return it != tiles.end() ? &it->properties : nullptr;
}

const TiledTileset* TiledMap::tilesetOf(u32 gid) const noexcept {
    const auto it =
        std::ranges::find_if(tilesets, [gid](const TiledTileset& tileset) { return tileset.contains(gid); });
    return it != tilesets.end() ? &*it : nullptr;
}

const TiledTileLayer* TiledMap::findTileLayer(std::string_view name) const noexcept {
    const auto it = std::ranges::find(tileLayers, name, &TiledTileLayer::name);
    return it != tileLayers.end() ? &*it : nullptr;
}

const TiledObjectLayer* TiledMap::findObjectLayer(std::string_view name) const noexcept {
    const auto it = std::ranges::find(objectLayers, name, &TiledObjectLayer::name);
    return it != objectLayers.end() ? &*it : nullptr;
}

Result<std::string> resolveRelativePath(std::string_view base, std::string_view relative) {
    if (relative.find("://") != std::string_view::npos) {
        return std::string(relative);
    }
    const usize schemeEnd = base.find("://");
    const usize rootLength = schemeEnd == std::string_view::npos ? 0 : schemeEnd + 3;
    std::vector<std::string_view> parts;
    const auto split = [&](std::string_view path) -> Result<void> {
        while (!path.empty()) {
            const usize slash = path.find_first_of("/\\");
            const std::string_view part = path.substr(0, slash);
            path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
            if (part.empty() || part == ".") {
                continue;
            }
            if (part == "..") {
                if (parts.empty()) {
                    return Unexpected(invalid(std::format("path '{}' from '{}'", relative, base),
                                              "it leaves the root of the mount point"));
                }
                parts.pop_back();
                continue;
            }
            parts.push_back(part);
        }
        return {};
    };
    const std::string_view baseFile = base.substr(rootLength);
    const usize lastSlash = baseFile.find_last_of("/\\");
    TESERRA_TRY(
        split(lastSlash == std::string_view::npos ? std::string_view{} : baseFile.substr(0, lastSlash)));
    TESERRA_TRY(split(relative));
    std::string resolved(base.substr(0, rootLength));
    for (usize i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            resolved += '/';
        }
        resolved += parts[i];
    }
    return resolved;
}

Result<TiledMap> parseTiledMap(std::span<const std::byte> bytes, std::string_view mapPath,
                               const TiledFileReader& readFile) {
    TESERRA_TRY_ASSIGN(const JsonDocument document, JsonDocument::parse(bytes));
    return readMap(document.root(), mapPath, readFile);
}

Result<TiledMap> TiledMapLoader::load(AssetLoadContext& context, std::span<const std::byte> bytes) {
    return parseTiledMap(bytes, context.path(),
                         [&](std::string_view virtualPath) { return context.readFile(virtualPath); });
}

} // namespace teserra::assets
