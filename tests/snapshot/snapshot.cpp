#include "snapshot.hpp"

#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/sampler.hpp>
#include <teserra/rhi/texture.hpp>
#include <teserra/test/gpu.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <format>
#include <string>

namespace teserra::snapshot {

namespace {

bool g_updateGoldens =
    false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): set once by main()

std::filesystem::path goldenDirectory() {
    return std::filesystem::path(TESERRA_GOLDEN_DIR);
}

std::string fileName(std::string_view name) {
    return std::string(name) + ".png";
}

rhi::Backend backendForTests() {
    const std::string_view name = test::gpuBackendName();
    if (name != "opengl") {
        FAIL("Unknown TESERRA_GPU_BACKEND '" << name << "' (known: opengl)");
    }
    return rhi::Backend::OpenGL;
}

} // namespace

void setUpdateGoldens(bool update) noexcept {
    g_updateGoldens = update;
}

bool updateGoldens() noexcept {
    return g_updateGoldens;
}

void checkSnapshot(std::string_view name, const Image& actual, const Tolerance& tolerance) {
    const std::filesystem::path golden = goldenDirectory() / fileName(name);
    if (g_updateGoldens) {
        const Result<void> written = writePng(golden, actual);
        REQUIRE(written.has_value());
        WARN("Updated golden " << golden.string());
        return;
    }
    const Result<Image> expected = readPng(golden);
    if (!expected) {
        FAIL("No golden for '" << name << "' (" << std::format("{}", expected.error())
                               << "). Run teserra_snapshot_tests --update-goldens and review the image.");
    }
    const Comparison result = compare(actual, *expected, tolerance);
    if (!result.passed) {
        const std::filesystem::path actualPath = goldenDirectory() / "_actual" / fileName(name);
        const std::filesystem::path diffPath = goldenDirectory() / "_diff" / fileName(name);
        static_cast<void>(writePng(actualPath, actual));
        if (result.sameSize) {
            static_cast<void>(writePng(diffPath, diffImage(actual, *expected, tolerance)));
        }
        INFO("actual: " << actualPath.string() << ", diff: " << diffPath.string());
    }
    INFO("snapshot '" << name << "': " << result.differingPixels << " pixels differ ("
                      << result.differingRatio * 100.0 << "%, allowed " << tolerance.maxDifferingRatio * 100.0
                      << "%), largest channel difference " << int{result.maxChannelDifference} << " (allowed "
                      << int{tolerance.channel} << ")");
    CHECK(result.sameSize);
    CHECK(result.passed);
}

SnapshotRenderer::SnapshotRenderer() {
    m_app = std::make_unique<engine::App>(engine::AppConfig{
        .title = "Teserra snapshot tests",
        .windowSize = kSnapshotSize,
        .windowVisible = false,
        .backend = backendForTests(),
        .spritePass = {.filter = rhi::Filter::Nearest},
    });
    if (!m_app->isInitialized()) {
        TESERRA_SKIP_WITHOUT_GPU(
            "No window system or GPU driver: " << std::format("{}", *m_app->initError()));
    }
    rhi::Device& gpu = m_app->device();
    m_output = {.texture =
                    *gpu.createTexture({.format = rhi::Format::Rgba8Unorm,
                                        .width = kSnapshotSize.x,
                                        .height = kSnapshotSize.y,
                                        .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
                                        .debugName = "Snapshot"}),
                .format = rhi::Format::Rgba8Unorm,
                .size = kSnapshotSize};
    m_readback = *gpu.createBuffer({.sizeInBytes = u64{kSnapshotSize.x} * kSnapshotSize.y * 4,
                                    .usage = rhi::BufferUsage::CopyDst,
                                    .memory = rhi::MemoryLocation::Readback,
                                    .debugName = "Snapshot readback"});
}

SnapshotRenderer::~SnapshotRenderer() {
    if (m_app->isInitialized()) {
        m_app->device().waitIdle();
        m_app->device().destroy(m_readback);
        m_app->device().destroy(m_output.texture);
    }
}

Image SnapshotRenderer::render(const std::function<void(rhi::CommandList&)>& record) {
    rhi::Device& gpu = device();
    gpu.beginFrame();
    rhi::CommandList& commands = gpu.acquireCommandList();
    record(commands);
    commands.copyTextureToBuffer({.buffer = m_readback,
                                  .texture = m_output.texture,
                                  .width = kSnapshotSize.x,
                                  .height = kSnapshotSize.y});
    gpu.submit(commands);
    gpu.endFrame();
    gpu.waitIdle();

    Image image{.width = kSnapshotSize.x,
                .height = kSnapshotSize.y,
                .rgba = std::vector<u8>(usize{kSnapshotSize.x} * kSnapshotSize.y * 4)};
    const std::span<std::byte> bytes = gpu.map(m_readback);
    REQUIRE(bytes.size() >= image.rgba.size());
    std::memcpy(image.rgba.data(), bytes.data(), image.rgba.size());
    return image;
}

} // namespace teserra::snapshot
