// Tiled maps: a map saved by Tiled 1.10 (JSON, CSV layer data) with an external and an embedded tileset,
// custom properties and a rectangle object loads with the right tiles and properties; unsupported features
// and broken data fail with errors that name the layer and the tile or object.

#include <teserra/assets/tiled.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/test/memory_file_system.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::assets;
using Catch::Matchers::ContainsSubstring;

namespace {

// As Tiled 1.10 saves a 4x3 map with an external tileset (terrain.tsj, gids 1-8) and an embedded one
// (gids 9-10).
constexpr std::string_view kLevel = R"({ "compressionlevel":-1,
 "height":3,
 "infinite":false,
 "layers":[
        {
         "data":[1, 1, 2, 2,
            1, 3, 3, 2,
            4, 4, 4, 4],
         "height":3,
         "id":1,
         "name":"ground",
         "opacity":1,
         "type":"tilelayer",
         "visible":true,
         "width":4,
         "x":0,
         "y":0
        },
        {
         "data":[0, 0, 9, 0,
            0, 0, 0, 0,
            0, 10, 0, 0],
         "height":3,
         "id":2,
         "name":"decor",
         "opacity":0.5,
         "properties":[
                {
                 "name":"parallax",
                 "type":"float",
                 "value":0.25
                }],
         "type":"tilelayer",
         "visible":false,
         "width":4,
         "x":0,
         "y":0
        },
        {
         "draworder":"topdown",
         "id":3,
         "name":"spawns",
         "objects":[
                {
                 "class":"spawn",
                 "height":16,
                 "id":1,
                 "name":"player",
                 "properties":[
                        {
                         "name":"facing",
                         "type":"string",
                         "value":"left"
                        }],
                 "rotation":0,
                 "visible":true,
                 "width":8,
                 "x":20,
                 "y":12.5
                }],
         "opacity":1,
         "type":"objectgroup",
         "visible":true,
         "x":0,
         "y":0
        }],
 "nextlayerid":4,
 "nextobjectid":2,
 "orientation":"orthogonal",
 "properties":[
        {
         "name":"music",
         "type":"file",
         "value":"..\/audio\/theme.ogg"
        },
        {
         "name":"night",
         "type":"bool",
         "value":true
        },
        {
         "name":"spawnCount",
         "type":"int",
         "value":3
        },
        {
         "name":"tint",
         "type":"color",
         "value":"#ff336699"
        }],
 "renderorder":"right-down",
 "tiledversion":"1.10.2",
 "tileheight":16,
 "tilesets":[
        {
         "firstgid":1,
         "source":"..\/tiles\/terrain.tsj"
        },
        {
         "columns":2,
         "firstgid":9,
         "image":"props.png",
         "imageheight":16,
         "imagewidth":32,
         "margin":0,
         "name":"props",
         "spacing":0,
         "tilecount":2,
         "tileheight":16,
         "tilewidth":16
        }],
 "tilewidth":16,
 "type":"map",
 "version":"1.10",
 "width":4
})";

constexpr std::string_view kTerrain = R"({ "columns":4,
 "image":"terrain.png",
 "imageheight":34,
 "imagewidth":70,
 "margin":1,
 "name":"terrain",
 "spacing":2,
 "tilecount":8,
 "tiledversion":"1.10.2",
 "tileheight":16,
 "tiles":[
        {
         "id":3,
         "properties":[
                {
                 "name":"solid",
                 "type":"bool",
                 "value":true
                }]
        }],
 "tilewidth":16,
 "type":"tileset",
 "version":"1.10"
})";

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

// Reads files from a map of virtual paths.
struct Files {
    std::map<std::string, std::string, std::less<>> byPath{
        {"assets://tiles/terrain.tsj", std::string(kTerrain)}};

    TiledFileReader reader() {
        return [this](std::string_view path) -> Result<std::vector<std::byte>> {
            const auto it = byPath.find(path);
            if (it == byPath.end()) {
                return fail(ErrorCode::NotFound, Module::Platform, "File '{}' not found", path);
            }
            return bytesOf(it->second);
        };
    }
};

Result<TiledMap> parse(std::string_view text, Files& files) {
    return parseTiledMap(bytesOf(text), "assets://maps/level.tmj", files.reader());
}

// kLevel with `from` replaced by `to`, which must occur in it.
std::string edited(std::string_view from, std::string_view to) {
    std::string text(kLevel);
    const usize at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

Error parseError(std::string_view text) {
    Files files;
    const Result<TiledMap> map = parse(text, files);
    REQUIRE_FALSE(map.has_value());
    return map.error();
}

} // namespace

TEST_CASE("Tiled: a map saved by Tiled loads with its tiles, tilesets, objects and properties",
          "[assets][tiled]") {
    Files files;
    const Result<TiledMap> loaded = parse(kLevel, files);
    REQUIRE(loaded.has_value());
    const TiledMap& map = loaded.value();
    CHECK(map.size == UVec2{4, 3});
    CHECK(map.tileSize == UVec2{16, 16});

    REQUIRE(map.tilesets.size() == 2);
    const TiledTileset& terrain = map.tilesets[0];
    CHECK(terrain.name == "terrain");
    CHECK(terrain.firstGid == 1);
    CHECK(terrain.image == "assets://tiles/terrain.png"); // relative to the .tsj
    CHECK(terrain.imageSize == UVec2{70, 34});
    CHECK(terrain.margin == 1);
    CHECK(terrain.spacing == 2);
    CHECK(terrain.columns == 4);
    CHECK(terrain.tileCount == 8);
    REQUIRE(terrain.tileProperties(3) != nullptr);
    CHECK(terrain.tileProperties(3)->getBool("solid") == true);
    CHECK(terrain.tileProperties(0) == nullptr);
    const TiledTileset& props = map.tilesets[1];
    CHECK(props.image == "assets://maps/props.png"); // relative to the map
    CHECK(props.firstGid == 9);
    CHECK(map.tilesetOf(4) == &terrain);
    CHECK(map.tilesetOf(10) == &props);
    CHECK(map.tilesetOf(0) == nullptr);
    CHECK(map.tilesetOf(11) == nullptr);

    REQUIRE(map.tileLayers.size() == 2);
    const TiledTileLayer& ground = map.tileLayers[0];
    CHECK(ground.name == "ground");
    CHECK(ground.size == UVec2{4, 3});
    CHECK(ground.gids == std::vector<u32>{1, 1, 2, 2, 1, 3, 3, 2, 4, 4, 4, 4});
    CHECK(ground.gid(1, 1) == 3); // column 1 of the middle row
    CHECK(ground.visible);
    const TiledTileLayer* decor = map.findTileLayer("decor");
    REQUIRE(decor != nullptr);
    CHECK_FALSE(decor->visible);
    CHECK(decor->opacity == 0.5F);
    CHECK(decor->properties.getNumber("parallax") == 0.25);
    CHECK(decor->gid(2, 0) == 9);

    const TiledObjectLayer* spawns = map.findObjectLayer("spawns");
    REQUIRE(spawns != nullptr);
    REQUIRE(spawns->objects.size() == 1);
    const TiledObject& player = spawns->objects[0];
    CHECK(player.id == 1);
    CHECK(player.name == "player");
    CHECK(player.type == "spawn");
    CHECK(player.position == Vec2{20.0F, 12.5F});
    CHECK(player.size == Vec2{8.0F, 16.0F});
    CHECK(player.properties.getString("facing") == "left");

    CHECK(map.properties.getString("music") == "assets://audio/theme.ogg");
    CHECK(map.properties.getBool("night") == true);
    CHECK(map.properties.getInt("spawnCount") == 3);
    CHECK(map.properties.getNumber("spawnCount") == 3.0);
    CHECK(map.properties.getString("tint") == "#ff336699");
    CHECK_FALSE(map.properties.getBool("music").has_value()); // a string, not a bool
    CHECK(map.properties.find("missing") == nullptr);
}

TEST_CASE("Tiled: the loader reads the map and its tileset through the mount points", "[assets][tiled]") {
    test::MemoryFileSystem files;
    files.addTextFile("/game/assets/maps/level.tmj", kLevel);
    files.addTextFile("/game/assets/tiles/terrain.tsj", kTerrain);
    MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));
    AssetRegistry registry{{.mounts = &mounts}};
    registry.registerLoader<TiledMap>(std::make_unique<TiledMapLoader>());

    const Asset<TiledMap> level = registry.load<TiledMap>("assets://maps/level.tmj");
    REQUIRE(level.isLoaded());
    CHECK(level.get().tilesets[0].tileCount == 8);
    CHECK(level.get().tileLayers.size() == 2);
}

TEST_CASE("Tiled: unsupported features fail with errors that name them", "[assets][tiled]") {
    const auto unsupported = [](std::string_view text, std::string_view expected) {
        const Error error = parseError(text);
        CHECK(error.code == ErrorCode::Unsupported);
        CHECK_THAT(error.message, ContainsSubstring(std::string(expected)));
    };
    unsupported(edited(R"("infinite":false)", R"("infinite":true)"), "map: infinite maps are not supported");
    unsupported(edited(R"("orientation":"orthogonal")", R"("orientation":"isometric")"),
                "isometric maps are not supported");
    unsupported(
        edited(R"("data":[1, 1, 2, 2,)", R"("encoding":"base64", "compression":"zlib", "data":[1, 1, 2, 2,)"),
        "layer 'ground': 'base64' layer data with compression is not supported");
    unsupported(edited("0, 10, 0, 0]", "0, 2147483658, 0, 0]"), // 10 | flipped horizontally
                "layer 'decor': tile (1, 2) is flipped or rotated");
    unsupported(edited(R"("type":"objectgroup")", R"("type":"group")"),
                "layer 'spawns': group layers are not supported");
    unsupported(edited(R"("type":"objectgroup")", R"("type":"imagelayer")"),
                "image layers are not supported");
    unsupported(edited(R"("rotation":0,)", R"("rotation":0, "ellipse":true,)"),
                "layer 'spawns': object 1 'player': ellipse objects are not supported, only rectangles");
    unsupported(edited(R"("source":"..\/tiles\/terrain.tsj")", R"("source":"..\/tiles\/terrain.tsx")"),
                "only JSON tilesets (.tsj) are supported");
    unsupported(edited(R"("type":"bool")", R"("type":"class")"),
                "map: property 'night': properties of type 'class'");
}

TEST_CASE("Tiled: broken data fails with the layer and tile", "[assets][tiled]") {
    const auto invalid = [](std::string_view text, std::string_view expected) {
        const Error error = parseError(text);
        CHECK(error.code == ErrorCode::InvalidData);
        CHECK_THAT(error.message, ContainsSubstring(std::string(expected)));
    };
    invalid(edited("4, 4, 4, 4]", "4, 4, 4, 11]"),
            "layer 'ground': tile (3, 2) has id 11, which is in no tileset");
    invalid(edited("4, 4, 4, 4]", "4, 4, 4]"), "layer 'ground': 'data' has 11 tiles, not 12 (4x3)");
    invalid(edited("4, 4, 4, 4]", "4, 4, 4, \"x\"]"), "layer 'ground': tile (3, 2) is not a tile id");
    invalid(edited(R"( "height":3,)", R"( "height":"three",)"),
            "map: 'height' must be an integer, not string");
    invalid(edited(R"("tilecount":2,)", R"("tilecount":0,)"), "tileset 1 'props': 'tilecount' is 0, outside");
    invalid(std::string(kLevel).substr(0, 200), "line ");
    invalid(edited(R"("source":"..\/tiles\/terrain.tsj")", R"("source":"..\/..\/..\/terrain.tsj")"),
            "leaves the root of the mount point");

    Files files;
    files.byPath.clear(); // the external tileset is missing
    const Result<TiledMap> missing = parse(kLevel, files);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK_THAT(missing.error().message, ContainsSubstring("tileset 'assets://tiles/terrain.tsj'"));
}

TEST_CASE("Tiled: relative paths resolve against the file that names them", "[assets][tiled]") {
    CHECK(resolveRelativePath("assets://maps/level.tmj", "../tiles/t.png") == "assets://tiles/t.png");
    CHECK(resolveRelativePath("assets://maps/level.tmj", "t.png") == "assets://maps/t.png");
    CHECK(resolveRelativePath("assets://maps/level.tmj", "./a/./b\\c.png") == "assets://maps/a/b/c.png");
    CHECK(resolveRelativePath("assets://level.tmj", "t.png") == "assets://t.png");
    CHECK(resolveRelativePath("assets://maps/level.tmj", "engine://fallback.png") == "engine://fallback.png");
    CHECK_FALSE(resolveRelativePath("assets://level.tmj", "../t.png").has_value());
}
