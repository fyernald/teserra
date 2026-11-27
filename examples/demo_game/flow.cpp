#include "flow.hpp"

#include <teserra/audio/audio_engine.hpp>
#include <teserra/core/log.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/platform/gamepad.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/scene/audio_components.hpp>
#include <teserra/scene/collision_debug.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/transform.hpp>

#include "art.hpp"
#include "font.hpp"

#include <format>
#include <optional>
#include <string>
#include <utility>

namespace demo {

using teserra::Color;
using teserra::LogCategory;
using teserra::Result;
namespace assets = teserra::assets;
namespace engine = teserra::engine;
namespace platform = teserra::platform;
namespace renderer = teserra::renderer;

namespace {

constexpr Color kTitleColor{.r = 1.0F, .g = 0.85F, .b = 0.3F, .a = 1.0F};
constexpr Color kTextColor{.r = 0.9F, .g = 0.9F, .b = 1.0F, .a = 1.0F};
constexpr Color kHintColor{.r = 0.55F, .g = 0.5F, .b = 0.7F, .a = 1.0F};
constexpr Color kBackground{.r = 0.09F, .g = 0.08F, .b = 0.13F, .a = 1.0F};

// Whether `button` was pressed on the first gamepad, if one is connected.
bool padPressed(const platform::Input& input, platform::GamepadButton button) {
    const std::optional<u32> pad = input.firstGamepad();
    return pad.has_value() && input.wasPressed(*pad, button);
}

// Enter, or A (South) / Start on a gamepad.
bool confirmPressed(const platform::Input& input) {
    return input.wasPressed(platform::Key::Enter) || padPressed(input, platform::GamepadButton::South) ||
           padPressed(input, platform::GamepadButton::Start);
}

// Escape, or B (East) on a gamepad.
bool backPressed(const platform::Input& input) {
    return input.wasPressed(platform::Key::Escape) || padPressed(input, platform::GamepadButton::East);
}

// The menu and the win screen: keys and gamepad buttons request the next screen.
void menuInput(ecs::SystemContext& ctx) {
    if (confirmPressed(ctx.resource<const platform::Input>())) {
        ctx.resource<ScreenRequest>().next = Screen::Level;
    }
}

void winInput(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    if (confirmPressed(input)) {
        ctx.resource<ScreenRequest>().next = Screen::Level;
    } else if (backPressed(input)) {
        ctx.resource<ScreenRequest>().next = Screen::Menu;
    }
}

void levelInput(ecs::SystemContext& ctx) {
    if (backPressed(ctx.resource<const platform::Input>())) {
        ctx.resource<ScreenRequest>().next = Screen::Menu;
    }
}

// A camera at the origin showing the 320x180 view.
void spawnCamera(scene::Scene& scene) {
    const ecs::Entity camera = scene.spawn();
    scene.add<scene::Transform>(camera);
    scene.add<scene::Camera>(camera, scene::Camera{.projection = scene::Orthographic{.height = kViewHeight},
                                                   .clearColor = kBackground});
}

// The tune, looping on the Music bus while the screen lasts.
void spawnMusic(scene::Scene& scene, const assets::Asset<teserra::audio::AudioClip>& tune) {
    const ecs::Entity music = scene.spawn();
    scene.add<scene::AudioSource>(
        music,
        scene::AudioSource{.clip = tune, .bus = teserra::audio::Bus::Music, .volume = 0.6F, .loop = true});
}

void spawnArt(scene::Scene& scene, const scene::Tileset& tileset, Cell cell, Vec2 position) {
    const ecs::Entity entity = scene.spawn();
    scene.add<scene::Transform>(entity, scene::Transform{.position = {position, 0.0F}});
    scene.add<scene::SpriteRenderer>(entity, scene::spriteFromSheet(tileset.sheet(), cell));
}

} // namespace

Flow::Flow(engine::App& app, const scene::Tileset& tileset)
    : m_app(app),
      m_tileset(tileset),
      m_menu(app.scenes().create("menu")),
      m_level(app.scenes().create("level")),
      m_win(app.scenes().create("win")),
      m_sounds(makeSounds(app.assets())) {
    // The level's systems play the effects.
    m_level.world().insertResource<Sounds>(m_sounds);
    for (const Screen screen : {Screen::Menu, Screen::Level, Screen::Win}) {
        scene::Scene& target = sceneOf(screen);
        target.world().insertResource<ScreenRequest>();
        target.onEnter([this, screen](scene::Scene& /*entered*/) { build(screen); });
        target.onExit([](scene::Scene& left) {
            left.reset();
            // The resources the content added; the font's texture is released with the sheet.
            if (left.world().hasResource<GameState>()) {
                left.world().removeResource<GameState>();
            }
            if (left.world().hasResource<ScreenFont>()) {
                left.world().removeResource<ScreenFont>();
            }
        });
    }
    m_menu.systems()
        .add(ecs::Phase::Update, "menu_input", menuInput)
        .readsResource<platform::Input>()
        .writesResource<ScreenRequest>();
    m_win.systems()
        .add(ecs::Phase::Update, "win_input", winInput)
        .readsResource<platform::Input>()
        .writesResource<ScreenRequest>();
    m_level.systems()
        .add(ecs::Phase::Update, "level_input", levelInput)
        .readsResource<platform::Input>()
        .writesResource<ScreenRequest>();
    addGameplaySystems(m_level);
}

scene::Scene& Flow::start() {
    m_active = Screen::Menu;
    m_logCountdown = 1; // after the first frame, which the menu's enter hook built
    return m_menu;
}

scene::Scene& Flow::sceneOf(Screen screen) {
    switch (screen) {
        case Screen::Menu:
            return m_menu;
        case Screen::Level:
            return m_level;
        case Screen::Win:
            return m_win;
    }
    return m_menu;
}

void Flow::update() {
    if (m_logCountdown > 0 && --m_logCountdown == 0) {
        logCounts("after entering a screen");
    }

    const platform::Input& input = m_app.input();
    if (input.wasPressed(platform::Key::M)) {
        teserra::audio::AudioEngine& audio = m_app.audio();
        audio.setBusMuted(teserra::audio::Bus::Music, !audio.isBusMuted(teserra::audio::Bus::Music));
    }
    if (m_active == Screen::Menu && input.wasPressed(platform::Key::Escape)) {
        m_app.quit();
        return;
    }
    if (m_active == Screen::Level) {
        if (input.wasPressed(platform::Key::F1)) {
            auto& debug = m_level.world().resource<scene::CollisionDebug>();
            debug.enabled = !debug.enabled;
        }
        if (const GameState* state = m_level.world().tryResource<GameState>();
            state != nullptr && state->won) {
            m_results = {.coins = state->coins,
                         .totalCoins = state->totalCoins,
                         .restarts = state->deaths,
                         .seconds = state->seconds};
            enter(Screen::Win);
            return;
        }
    }
    auto& request = sceneOf(m_active).world().resource<ScreenRequest>();
    if (request.next) {
        const Screen next = *request.next;
        request.next.reset();
        enter(next);
    }
}

void Flow::enter(Screen screen) {
    m_app.scenes().switchTo(sceneOf(screen));
    m_active = screen;
    // The hooks run at the start of the next frame; the assets only the left screen used unload at its end.
    m_logCountdown = 2;
}

Result<renderer::SpriteSheet> Flow::font() {
    const assets::Asset<renderer::Texture> texture =
        m_app.assets().load<renderer::Texture>("game://font", [&] {
            TESERRA_LOG_INFO(LogCategory::App, "Loaded the font");
            return renderer::Texture::create(m_app.device(), {fontWidth(), kGlyphHeight}, drawFont(),
                                             teserra::rhi::Format::Rgba8Srgb, "Font");
        });
    return renderer::SpriteSheet::grid(texture, {.cellSize = {kGlyphWidth, kGlyphHeight},
                                                 .count = static_cast<u32>(kFontCharacters.size())});
}

void Flow::build(Screen screen) {
    scene::Scene& target = sceneOf(screen);
    if (screen != Screen::Win) {
        spawnMusic(target, m_sounds.tune);
    }
    if (screen == Screen::Level) {
        buildLevel(target, m_tileset);
        ++m_plays;
        TESERRA_LOG_INFO(LogCategory::App, "Play {}", m_plays);
        return;
    }
    Result<renderer::SpriteSheet> glyphs = font();
    if (!glyphs) {
        TESERRA_LOG_ERROR(LogCategory::App, "Cannot cut the font: {}", glyphs.error());
        return;
    }
    const renderer::SpriteSheet& sheet =
        target.world().insertResource<ScreenFont>(ScreenFont{.glyphs = std::move(*glyphs)}).glyphs;
    spawnCamera(target);
    if (screen == Screen::Menu) {
        spawnText(target, sheet, "COIN CAVE", {0.0F, 2.5F}, 3.0F, kTitleColor);
        spawnArt(target, m_tileset, Hero, {-1.0F, 0.0F});
        spawnArt(target, m_tileset, Coin, {0.0F, 0.0F});
        spawnArt(target, m_tileset, Slime, {1.0F, 0.0F});
        spawnText(target, sheet, "PRESS ENTER", {0.0F, -2.0F}, 2.0F, kTextColor);
        spawnText(target, sheet, "ESC: QUIT", {0.0F, -4.0F}, 1.0F, kHintColor);
        return;
    }
    spawnText(target, sheet, "YOU WIN!", {0.0F, 3.0F}, 3.0F, kTitleColor);
    spawnText(target, sheet, std::format("COINS {}/{}", m_results.coins, m_results.totalCoins), {0.0F, 1.0F},
              2.0F, kTextColor);
    spawnText(target, sheet, std::format("TIME {:.1f}", m_results.seconds), {0.0F, 0.0F}, 2.0F, kTextColor);
    spawnText(target, sheet, std::format("RESTARTS {}", m_results.restarts), {0.0F, -1.0F}, 2.0F, kTextColor);
    spawnText(target, sheet, "ENTER: PLAY AGAIN   ESC: MENU", {0.0F, -3.5F}, 1.0F, kHintColor);
}

void Flow::logCounts(const char* moment) {
    TESERRA_LOG_INFO(LogCategory::App, "Counts {}: {} assets; entities: menu {}, level {}, win {}", moment,
                     m_app.assets().size(), m_menu.world().entityCount(), m_level.world().entityCount(),
                     m_win.world().entityCount());
}

} // namespace demo
