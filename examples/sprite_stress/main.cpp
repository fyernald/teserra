// Sprite stress: the first public feedback build (iteration 8, #60). Thousands of moving, spinning sprites
// with 16 textures are drawn by the renderer's sprite pass in one draw call per texture, under a 2D camera
// that pans and zooms. The window title shows the sprite count, FPS, draw calls and batches; a red square
// marks the window's top-left corner, so testers can see that the image is not flipped.
//
//   teserra_sprite_stress                       1000 sprites until Escape or close
//   teserra_sprite_stress --sprites 10000       another count
//   teserra_sprite_stress --frames 600 --hidden-window --sprites 10000   smoke run
//
// Keys: 1 / 2 switch between 1000 and 10 000 sprites; WASD or the arrows pan; the mouse wheel zooms around
// the cursor, + and - around the center; R resets the camera; Escape quits. Vsync is off, so the FPS counter
// shows what the machine can do. Everything that is logged also goes to teserra.log.

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/renderer/camera.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/renderer/sprite_pass.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/texture.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <numbers>
#include <random>
#include <span>
#include <string_view>
#include <vector>

using namespace teserra;

namespace {

constexpr u32 kTextureCount = 16;
constexpr u32 kTextureSize = 16;
constexpr u32 kDefaultSprites = 1000;
constexpr u32 kManySprites = 10'000;
constexpr f32 kPanPixelsPerSecond = 600.0F;
constexpr f32 kTitleIntervalSeconds = 0.5F;
constexpr f32 kMarkerPixels = 24.0F;

// The value of `--sprites N`, or the default.
u32 spriteCountArgument(const engine::CommandLine& args) {
    for (int i = 1; i + 1 < args.argc; ++i) {
        if (std::string_view(args.argv[i]) ==
            "--sprites") { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const std::string_view value =
                args.argv[i + 1]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            u32 count = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
            if (error == std::errc{} && end == value.data() + value.size() && count > 0) {
                return count;
            }
            TESERRA_LOG_WARN(LogCategory::App, "--sprites needs a positive number, not '{}'; using {}", value,
                             kDefaultSprites);
        }
    }
    return kDefaultSprites;
}

// A fully saturated color of hue `hue` (0 to 1), as sRGB bytes.
std::array<u8, 3> hueColor(f32 hue) {
    const auto channel = [&](f32 offset) {
        const f32 value =
            std::clamp(std::abs(std::fmod((hue * 6.0F) + offset, 6.0F) - 3.0F) - 1.0F, 0.0F, 1.0F);
        return static_cast<u8>(std::lround(value * 255.0F));
    };
    return {channel(0.0F), channel(4.0F), channel(2.0F)};
}

// A 16x16 disc of `color` with a soft edge and a lighter center, RGBA8 sRGB, top row first.
std::vector<std::byte> discTexels(const std::array<u8, 3>& color) {
    std::vector<std::byte> texels;
    texels.reserve(usize{kTextureSize} * kTextureSize * 4);
    constexpr f32 kCenter = (kTextureSize - 1) * 0.5F;
    for (u32 y = 0; y < kTextureSize; ++y) {
        for (u32 x = 0; x < kTextureSize; ++x) {
            const f32 distance =
                std::hypot(static_cast<f32>(x) - kCenter, static_cast<f32>(y) - kCenter) / kCenter;
            const f32 alpha = std::clamp((1.0F - distance) * 4.0F, 0.0F, 1.0F);
            const f32 light = std::clamp(0.6F - distance, 0.0F, 0.6F); // a highlight toward the middle
            for (const u8 channel : color) {
                texels.push_back(static_cast<std::byte>(
                    std::lround(static_cast<f32>(channel) + (static_cast<f32>(255 - channel) * light))));
            }
            texels.push_back(static_cast<std::byte>(std::lround(alpha * 255.0F)));
        }
    }
    return texels;
}

struct Mover {
    Vec2 position{0.0F};
    Vec2 velocity{0.0F};
    f32 angle = 0.0F;
    f32 spin = 0.0F;
    f32 size = 32.0F;
    u32 texture = 0;
};

Mat4 spriteWorld(Vec2 position, f32 angle) {
    const f32 cosine = std::cos(angle);
    const f32 sine = std::sin(angle);
    Mat4 world{1.0F};
    world[0] = Vec4{cosine, sine, 0.0F, 0.0F};
    world[1] = Vec4{-sine, cosine, 0.0F, 0.0F};
    world[3] = Vec4{position, 0.0F, 1.0F};
    return world;
}

// The demo's GPU objects and state, created on the App's device and destroyed before it.
class SpriteStress {
public:
    SpriteStress(rhi::Device& device, UVec2 worldSize, u32 spriteCount)
        : m_device(device), m_halfWorld(Vec2{worldSize} * 0.5F) {
        reset(spriteCount);
    }
    ~SpriteStress() {
        m_renderer.reset(); // its passes hold bind groups of the textures
        for (const rhi::TextureHandle texture : m_textures) {
            m_device.destroy(texture);
        }
        m_device.destroy(m_marker);
    }
    SpriteStress(const SpriteStress&) = delete;
    SpriteStress& operator=(const SpriteStress&) = delete;
    SpriteStress(SpriteStress&&) = delete;
    SpriteStress& operator=(SpriteStress&&) = delete;

    [[nodiscard]] Result<void> create() {
        for (u32 i = 0; i < kTextureCount; ++i) {
            TESERRA_TRY_ASSIGN(
                const rhi::TextureHandle texture,
                m_device.createTexture(
                    {.width = kTextureSize, .height = kTextureSize, .debugName = std::format("Disc {}", i)}));
            m_textures.push_back(texture);
            m_device.writeTexture(texture, {.width = kTextureSize, .height = kTextureSize},
                                  discTexels(hueColor(static_cast<f32>(i) / kTextureCount)));
        }
        TESERRA_TRY_ASSIGN(m_marker,
                           m_device.createTexture({.width = 1, .height = 1, .debugName = "Marker"}));
        constexpr std::array kWhite{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
        m_device.writeTexture(m_marker, {.width = 1, .height = 1}, kWhite);

        TESERRA_TRY_ASSIGN(m_renderer, renderer::Renderer::create(m_device));
        TESERRA_TRY_ASSIGN(std::unique_ptr<renderer::SpritePass> sprites,
                           renderer::SpritePass::create(m_device, m_renderer->frameGroupLayout()));
        m_sprites = sprites.get();
        m_renderer->addPass(std::move(sprites));
        return {};
    }

    // New sprites at random places in the world box, moving and spinning; the same layout every run.
    void reset(u32 count) {
        std::mt19937 random(8); // NOLINT(bugprone-random-generator-seed): every tester sees the same layout
        std::uniform_real_distribution<f32> unit(-1.0F, 1.0F);
        m_movers.resize(count);
        for (u32 i = 0; i < count; ++i) {
            Mover& mover = m_movers[i];
            mover.position = Vec2{unit(random), unit(random)} * m_halfWorld;
            mover.velocity = Vec2{unit(random), unit(random)} * 150.0F;
            mover.angle = unit(random) * std::numbers::pi_v<f32>;
            mover.spin = unit(random) * 2.0F;
            mover.size = 20.0F + ((unit(random) + 1.0F) * 14.0F);
            mover.texture = i % kTextureCount;
        }
        m_packet.sprites.reserve(count + 1); // the marker too
        TESERRA_LOG_INFO(LogCategory::App, "{} sprites", count);
    }

    void update(engine::App& app, f32 deltaSeconds) {
        const platform::Input& input = app.input();
        if (input.wasPressed(platform::Key::Digit1)) {
            reset(kDefaultSprites);
        }
        if (input.wasPressed(platform::Key::Digit2)) {
            reset(kManySprites);
        }
        updateCamera(app, deltaSeconds);
        for (Mover& mover : m_movers) {
            mover.position += mover.velocity * deltaSeconds;
            mover.angle += mover.spin * deltaSeconds;
            for (int axis = 0; axis < 2; ++axis) { // bounce off the world box
                if (std::abs(mover.position[axis]) > m_halfWorld[axis]) {
                    mover.position[axis] = std::copysign(m_halfWorld[axis], mover.position[axis]);
                    mover.velocity[axis] = -mover.velocity[axis];
                }
            }
        }
        updateTitle(app, deltaSeconds);
    }

    void render(engine::App& app, rhi::CommandList& commands) {
        const UVec2 size = app.device().swapchain()->size();
        m_packet.clear();
        m_packet.views.push_back(renderer::renderView(m_camera.camera(size), m_camera.world(), size));
        // One sort depth for all, so sprites batch by texture: 16 draws. Sorting by -y would interleave the
        // textures and break a batch at nearly every sprite, the cost that texture atlases avoid in games.
        for (const Mover& mover : m_movers) {
            m_packet.sprites.push_back({.world = spriteWorld(mover.position, mover.angle),
                                        .size = Vec2{mover.size},
                                        .texture = m_textures[mover.texture]});
        }
        // The orientation marker: a red square in the window's top-left corner, above every sprite.
        const Vec2 corner = m_camera.screenToWorld(Vec2{kMarkerPixels}, size);
        m_packet.sprites.push_back({.world = spriteWorld(corner, 0.0F),
                                    .size = Vec2{kMarkerPixels / m_camera.pixelsPerWorldUnit()},
                                    .tint = {.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F},
                                    .texture = m_marker,
                                    .layer = 1000});
        m_renderer->render(
            m_packet,
            {.texture = app.backbuffer(), .format = app.device().swapchain()->format(), .size = size},
            commands);
    }

private:
    void updateCamera(engine::App& app, f32 deltaSeconds) {
        const platform::Input& input = app.input();
        const UVec2 viewport = app.device().swapchain()->size();
        if (input.wasPressed(platform::Key::R)) {
            m_camera = {};
        }
        const Vec2 pan =
            input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W) +
            input.axis2D(platform::Key::Left, platform::Key::Right, platform::Key::Down, platform::Key::Up);
        m_camera.pan(pan * (kPanPixelsPerSecond * deltaSeconds / m_camera.pixelsPerWorldUnit()));

        // The cursor is in window coordinates; the viewport in framebuffer pixels, which differ on high-DPI
        // screens.
        const UVec2 windowSize = app.window().windowSize();
        const Vec2 scale =
            windowSize.x > 0 && windowSize.y > 0 ? Vec2{viewport} / Vec2{windowSize} : Vec2{1.0F};
        if (const f32 scroll = input.scrollDelta().y; scroll != 0.0F) {
            m_camera.zoomAt(std::pow(1.15F, scroll), input.mousePosition() * scale, viewport);
        }
        const Vec2 center = Vec2{viewport} * 0.5F;
        if (input.wasPressed(platform::Key::Equal)) {
            m_camera.zoomAt(1.25F, center, viewport);
        }
        if (input.wasPressed(platform::Key::Minus)) {
            m_camera.zoomAt(0.8F, center, viewport);
        }
    }

    void updateTitle(engine::App& app, f32 deltaSeconds) {
        m_titleSeconds += deltaSeconds;
        if (m_titleSeconds < kTitleIntervalSeconds) {
            return;
        }
        m_titleSeconds = 0.0F;
        const renderer::SpritePassStats& stats = m_sprites->stats();
        app.window().setTitle(
            std::format("Teserra: sprite stress | {} sprites | {:.0f} FPS | {} draws | {} batches",
                        m_movers.size(), app.frameTimer().fps(), stats.drawCalls, stats.batches));
    }

    rhi::Device& m_device;
    Vec2 m_halfWorld;
    std::vector<rhi::TextureHandle> m_textures;
    rhi::TextureHandle m_marker;
    std::unique_ptr<renderer::Renderer> m_renderer;
    renderer::SpritePass* m_sprites = nullptr;
    renderer::Camera2D m_camera{.pixelsPerUnit = 1.0F}; // one world unit per pixel at zoom 1
    renderer::FramePacket m_packet;
    std::vector<Mover> m_movers;
    f32 m_titleSeconds = kTitleIntervalSeconds; // the first frame sets the title
};

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    if (const Result<void> logged = log::configure({.filePath = "teserra.log"}); !logged) {
        TESERRA_LOG_WARN(LogCategory::App, "No log file: {}", logged.error());
    }

    engine::App app{{.title = "Teserra: sprite stress", .vsync = false, .args = {argc, argv}}};
    if (!app.isInitialized()) {
        log::flush();
        return 1;
    }
    SpriteStress demo(app.device(), app.device().swapchain()->size(), spriteCountArgument({argc, argv}));
    if (const Result<void> created = demo.create(); !created) {
        TESERRA_LOG_ERROR(LogCategory::App, "Could not create the demo: {}", created.error());
        log::flush();
        return 1;
    }
    TESERRA_LOG_INFO(
        LogCategory::App,
        "Sprite stress with the {} backend: 1 / 2 for 1000 / 10 000 sprites, WASD / arrows pan, wheel "
        "and + / - zoom, R resets, Escape quits",
        rhi::backendName(app.device().backend()));

    const int exitCode = app.run({
        .update =
            [&](engine::App& running, f32 deltaSeconds) {
                if (running.input().wasPressed(platform::Key::Escape)) {
                    running.quit();
                }
                demo.update(running, deltaSeconds);
            },
        .render = [&](engine::App& running, rhi::CommandList& commands) { demo.render(running, commands); },
    });
    log::flush();
    return exitCode;
}
