// The renderer's asset loaders on the null device: textures from PNG files, shaders from .tshader blobs,
// GPU objects destroyed when their assets unload, and the fallbacks of failed loads.

#include <teserra/renderer/assets.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/log.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>
#include <teserra/test/png.hpp>

#include <catch2/catch_test_macros.hpp>
#include <renderer_tests/shaders.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

// 2 x 1 RGBA: opaque red, half-transparent blue.
constexpr std::array<u8, 8> kPixels{255, 0, 0, 255, 0, 0, 255, 128};

struct Fixture {
    Fixture() {
        REQUIRE(mounts.mount("assets", "/game/assets"));
        files.addFile("/game/assets/hero.png", test::writePng(2, 1, test::PngColor::Rgba, kPixels));
        files.addFile("/game/assets/camera.tshader",
                      {test::shaders::kCameraBlockVert.begin(), test::shaders::kCameraBlockVert.end()});
        files.addTextFile("/game/assets/broken.png", "not a PNG");
        files.addTextFile("/game/assets/broken.tshader", "not a shader");
        REQUIRE(registerAssetLoaders(registry, *device));
    }

    std::unique_ptr<rhi::null::NullDevice> device =
        rhi::null::createHeadlessDevice({.enableValidation = true});
    test::MemoryFileSystem files;
    assets::MountTable mounts{files};
    assets::AssetRegistry registry{{.mounts = &mounts}};
};

usize errorCount(const test::LogCapture& logs) {
    return static_cast<usize>(std::ranges::count_if(
        logs.records(), [](const test::CapturedLog& record) { return record.level >= LogLevel::Error; }));
}

} // namespace

TEST_CASE("Renderer assets: TextureLoader uploads a PNG file into an sRGB texture", "[renderer][assets]") {
    Fixture fixture;

    assets::Asset<Texture> hero = fixture.registry.load<Texture>("assets://hero.png");

    REQUIRE(hero.isReady());
    CHECK(hero.get().size() == UVec2{2, 1});
    const rhi::TextureHandle handle = hero.get().handle();
    const rhi::TextureDesc* desc = fixture.device->texture(handle);
    REQUIRE(desc != nullptr);
    CHECK(desc->format == rhi::Format::Rgba8Srgb);
    CHECK(desc->width == 2);
    CHECK(desc->height == 1);
    CHECK(desc->mipLevels == 1);
    CHECK(desc->debugName == "assets://hero.png");
    const std::span<const std::byte> texels = fixture.device->textureData(handle);
    REQUIRE(texels.size() == kPixels.size());
    CHECK(std::ranges::equal(texels, kPixels,
                             [](std::byte texel, u8 pixel) { return texel == std::byte{pixel}; }));

    SECTION("the texture is destroyed when the asset unloads at the end of the frame") {
        hero = {};
        CHECK(fixture.device->texture(handle) != nullptr);
        fixture.registry.endFrame();
        CHECK(fixture.device->texture(handle) == nullptr);
    }
}

TEST_CASE("Renderer assets: import settings make a linear texture with mipmaps", "[renderer][assets]") {
    Fixture fixture;
    const std::array<u8, 16> pixels{};
    fixture.files.addFile("/game/assets/normals.png", test::writePng(2, 2, test::PngColor::Rgba, pixels));
    fixture.files.addTextFile("/game/assets/normals.png.asset.json", R"({"srgb": false, "mipmaps": true})");
    fixture.files.addFile("/game/assets/mask.png", test::writePng(2, 2, test::PngColor::Rgba, pixels));
    fixture.files.addTextFile("/game/assets/mask.png.asset.json", R"({"srgb": false})");

    const assets::Asset<Texture> normals = fixture.registry.load<Texture>("assets://normals.png");
    REQUIRE(normals.isLoaded());
    const rhi::TextureDesc* desc = fixture.device->texture(normals.get().handle());
    REQUIRE(desc != nullptr);
    CHECK(desc->format == rhi::Format::Rgba8Unorm);
    CHECK(rhi::mipLevelCount(*desc) == 2); // 2x2 and 1x1

    const assets::Asset<Texture> mask = fixture.registry.load<Texture>("assets://mask.png");
    REQUIRE(mask.isLoaded());
    CHECK(fixture.device->texture(mask.get().handle())->format == rhi::Format::Rgba8Unorm);
    CHECK(fixture.device->texture(mask.get().handle())->mipLevels == 1);
}

TEST_CASE("Renderer assets: ShaderLoader creates a shader module of the blob's stage", "[renderer][assets]") {
    Fixture fixture;

    assets::Asset<Shader> camera = fixture.registry.load<Shader>("assets://camera.tshader");

    REQUIRE(camera.isReady());
    CHECK(camera.get().stage() == rhi::ShaderStage::Vertex);
    const rhi::ShaderModuleHandle handle = camera.get().handle();
    const rhi::ShaderModuleDesc* desc = fixture.device->shaderModule(handle);
    REQUIRE(desc != nullptr);
    CHECK(desc->stage == rhi::ShaderStage::Vertex);
    CHECK(desc->debugName == "assets://camera.tshader");

    camera = {};
    fixture.registry.endFrame();
    CHECK(fixture.device->shaderModule(handle) == nullptr);
}

TEST_CASE("Renderer assets: invalid files fail once and create no GPU object", "[renderer][assets]") {
    Fixture fixture;
    const usize objects = fixture.device->liveObjectCount();
    const test::LogCapture logs;

    const assets::Asset<Texture> texture = fixture.registry.load<Texture>("assets://broken.png");
    const assets::Asset<Shader> shader = fixture.registry.load<Shader>("assets://broken.tshader");
    const assets::Asset<Texture> missing = fixture.registry.load<Texture>("assets://missing.png");
    const assets::Asset<Texture> again = fixture.registry.load<Texture>("assets://missing.png");

    CHECK(texture.isFallback());
    CHECK(missing.isFallback());
    // A shader path without its stage has no fallback.
    CHECK_FALSE(shader.isReady());
    CHECK(fixture.device->liveObjectCount() == objects);
    CHECK(errorCount(logs) == 3);
}

TEST_CASE("Renderer assets: a missing texture resolves to the magenta checkerboard", "[renderer][assets]") {
    Fixture fixture;

    const assets::Asset<Texture> missing = fixture.registry.load<Texture>("assets://missing.png");
    const assets::Asset<Texture> broken = fixture.registry.load<Texture>("assets://broken.png");

    REQUIRE(missing.isFallback());
    CHECK(&missing.get() == &broken.get()); // created once
    CHECK(missing.get().size() == UVec2{kFallbackTextureSize});
    const rhi::TextureDesc* desc = fixture.device->texture(missing.get().handle());
    REQUIRE(desc != nullptr);
    CHECK(desc->format == rhi::Format::Rgba8Srgb);
    const std::span<const std::byte> texels = fixture.device->textureData(missing.get().handle());
    REQUIRE(texels.size() == usize{kFallbackTextureSize} * kFallbackTextureSize * 4);
    const auto texel = [&](u32 x, u32 y) {
        const usize offset = (usize{y} * kFallbackTextureSize + x) * 4;
        return std::array<u8, 4>{static_cast<u8>(texels[offset]), static_cast<u8>(texels[offset + 1]),
                                 static_cast<u8>(texels[offset + 2]), static_cast<u8>(texels[offset + 3])};
    };
    constexpr std::array<u8, 4> kMagenta{255, 0, 255, 255};
    constexpr std::array<u8, 4> kBlack{0, 0, 0, 255};
    CHECK(texel(0, 0) == kMagenta);
    CHECK(texel(kFallbackCheckSize - 1, kFallbackCheckSize - 1) == kMagenta);
    CHECK(texel(kFallbackCheckSize, 0) == kBlack);
    CHECK(texel(0, kFallbackCheckSize) == kBlack);
    CHECK(texel(kFallbackCheckSize, kFallbackCheckSize) == kMagenta);
    CHECK(texel(kFallbackTextureSize - 1, 0) == kBlack);

    SECTION("the fallback outlives the failed slots") {
        const rhi::TextureHandle handle = missing.get().handle();
        fixture.registry.endFrame();
        CHECK(fixture.device->texture(handle) != nullptr);
    }
}

TEST_CASE("Renderer assets: missing shaders resolve to the error shader of their stage",
          "[renderer][assets]") {
    Fixture fixture;

    const assets::Asset<Shader> vertex =
        fixture.registry.load<Shader>("assets://shaders/sprite.vert.tshader");
    const assets::Asset<Shader> fragment =
        fixture.registry.load<Shader>("assets://shaders/sprite.frag.tshader");

    REQUIRE(vertex.isFallback());
    REQUIRE(fragment.isFallback());
    CHECK(vertex.get().stage() == rhi::ShaderStage::Vertex);
    CHECK(fragment.get().stage() == rhi::ShaderStage::Fragment);
    const rhi::ShaderReflection* vertexReflection = fixture.device->shaderReflection(vertex.get().handle());
    const rhi::ShaderReflection* fragmentReflection =
        fixture.device->shaderReflection(fragment.get().handle());
    REQUIRE(vertexReflection != nullptr);
    REQUIRE(fragmentReflection != nullptr);
    // No inputs or bindings, so they fit any vertex layout and pipeline layout.
    CHECK(vertexReflection->vertexInputs.empty());
    CHECK(vertexReflection->bindings.empty());
    CHECK(fragmentReflection->bindings.empty());
}

TEST_CASE("Renderer assets: a shader file is not a texture", "[renderer][assets]") {
    Fixture fixture;
    const test::LogCapture logs;

    const assets::Asset<Texture> wrong = fixture.registry.load<Texture>("assets://camera.tshader");

    CHECK(wrong.isFallback());
    REQUIRE(errorCount(logs) == 1);
    CHECK(logs.records().front().message.find("'.tshader'") != std::string::npos);
}

TEST_CASE("Renderer assets: Texture::create makes a texture from texels", "[renderer][assets]") {
    Fixture fixture;
    const std::array<std::byte, 8> texels{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                          std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    Result<Texture> made = Texture::create(*fixture.device, {2, 1}, texels, rhi::Format::Rgba8Unorm, "Made");
    REQUIRE(made.has_value());
    CHECK(made->size() == UVec2{2, 1});
    const rhi::TextureDesc* desc = fixture.device->texture(made->handle());
    REQUIRE(desc != nullptr);
    CHECK(desc->format == rhi::Format::Rgba8Unorm);
    CHECK(desc->debugName == "Made");
    CHECK(std::ranges::equal(fixture.device->textureData(made->handle()), texels));

    const Result<Texture> wrong = Texture::create(*fixture.device, {2, 2}, texels);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().message == "Texture '' of 2x2 texels needs 16 bytes, not 8");
    CHECK_FALSE(Texture::create(*fixture.device, {0, 1}, {}).has_value());
}
