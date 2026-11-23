// AssetId: the normalized path and its FNV-1a hash.

#include <teserra/assets/asset_id.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string_view>
#include <unordered_set>

using namespace teserra;
using namespace teserra::assets;

TEST_CASE("normalizeAssetPath lowercases the scheme and turns backslashes into slashes",
          "[assets][asset_id]") {
    CHECK(normalizeAssetPath("assets://sprites/hero.png") == "assets://sprites/hero.png");
    CHECK(normalizeAssetPath("ASSETS://Sprites\\Hero.png") == "assets://Sprites/Hero.png");
    CHECK(normalizeAssetPath("Engine://shaders\\sprite.tshader") == "engine://shaders/sprite.tshader");
    // Without a scheme nothing is lowercased; file names may be case-sensitive.
    CHECK(normalizeAssetPath("Sprites\\Hero.png") == "Sprites/Hero.png");
    CHECK(normalizeAssetPath("").empty());
}

TEST_CASE("AssetId::fromPath hashes the normalized path with FNV-1a", "[assets][asset_id]") {
    // Reference values of FNV-1a 64-bit.
    STATIC_CHECK(AssetId::fromPath("").value == 0xcbf2'9ce4'8422'2325ULL);
    STATIC_CHECK(AssetId::fromPath("a").value == 0xaf63'dc4c'8601'ec8cULL);

    STATIC_CHECK(AssetId::fromPath("ASSETS://sprites\\hero.png") ==
                 AssetId::fromPath("assets://sprites/hero.png"));
    CHECK(AssetId::fromPath("ASSETS://Sprites\\Hero.png") ==
          AssetId::fromPath(normalizeAssetPath("ASSETS://Sprites\\Hero.png")));

    // The rest of the path keeps its case, and different paths have different IDs.
    CHECK(AssetId::fromPath("assets://hero.png") != AssetId::fromPath("assets://Hero.png"));
    CHECK(AssetId::fromPath("assets://hero.png") != AssetId::fromPath("engine://hero.png"));
}

TEST_CASE("AssetId is null by default, hashable and formats as hexadecimal", "[assets][asset_id]") {
    STATIC_CHECK(AssetId{}.isNull());
    STATIC_CHECK_FALSE(AssetId::fromPath("assets://a.png").isNull());

    const std::unordered_set<AssetId> ids{AssetId::fromPath("assets://a.png"),
                                          AssetId::fromPath("assets://b.png"),
                                          AssetId::fromPath("ASSETS://a.png")};
    CHECK(ids.size() == 2);

    CHECK(std::format("{}", AssetId{0xc0ffee}) == "0x0000000000c0ffee");
}
