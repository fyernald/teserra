// spriteFromSheet(): a SpriteRenderer showing one region of a sprite sheet.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/render_components.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>

using namespace teserra;
using namespace teserra::scene;

TEST_CASE("Scene: spriteFromSheet() shows a region's texture, UVs and world size", "[scene][render]") {
    const std::unique_ptr<rhi::null::NullDevice> device = rhi::null::createHeadlessDevice({});
    assets::AssetRegistry registry;
    const assets::Asset<renderer::Texture> texture =
        registry.load<renderer::Texture>("game://sheet", [&]() -> Result<renderer::Texture> {
            TESERRA_TRY_ASSIGN(const rhi::TextureHandle handle,
                               device->createTexture({.width = 64, .height = 32}));
            return renderer::Texture(*device, handle, {64, 32});
        });
    const renderer::SpriteSheet sheet(texture, {{.name = "hero", .position = {0, 0}, .size = {16, 32}},
                                                {.name = "coin", .position = {48, 16}, .size = {16, 16}}});

    const SpriteRenderer hero = spriteFromSheet(sheet, 0);
    CHECK(hero.texture == texture.get().handle());
    CHECK(hero.uv == renderer::UvRect{.min = {0.0F, 0.0F}, .max = {0.25F, 1.0F}});
    CHECK(hero.size == Vec2{1.0F, 2.0F});
    CHECK(hero.pivot == Vec2{0.5F});
    CHECK(hero.layer == 0);

    const std::optional<SpriteRenderer> coin = spriteFromSheet(sheet, "coin");
    REQUIRE(coin.has_value());
    CHECK(coin.value().uv == renderer::UvRect{.min = {0.75F, 0.5F}, .max = {1.0F, 1.0F}});
    CHECK(coin.value().size == Vec2{1.0F});
    CHECK_FALSE(spriteFromSheet(sheet, "gem").has_value());
}
