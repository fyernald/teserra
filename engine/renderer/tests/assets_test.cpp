// The renderer's asset loaders on the null device: textures from PNG files, shaders from .tshader blobs,
// and GPU objects destroyed when their assets unload.

#include <teserra/renderer/assets.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/log.hpp>
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
        registerAssetLoaders(registry, *device);
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

    CHECK_FALSE(texture.isReady());
    CHECK_FALSE(shader.isReady());
    CHECK_FALSE(missing.isReady());
    CHECK(fixture.device->liveObjectCount() == objects);
    CHECK(errorCount(logs) == 3);
}

TEST_CASE("Renderer assets: a shader file is not a texture", "[renderer][assets]") {
    Fixture fixture;
    const test::LogCapture logs;

    const assets::Asset<Texture> wrong = fixture.registry.load<Texture>("assets://camera.tshader");

    CHECK_FALSE(wrong.isReady());
    REQUIRE(errorCount(logs) == 1);
    CHECK(logs.records().front().message.find("'.tshader'") != std::string::npos);
}
