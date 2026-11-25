#include "flow.hpp"

#include <teserra/core/log.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/scene/collision_debug.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/transform.hpp>

#include "art.hpp"
#include "font.hpp"

#include <format>
#include <string>
#include <utility>
#include <vector>

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

// The menu and the win screen: keys request the next screen.
void menuInput(ecs::SystemContext& ctx) {
    if (ctx.resource<const platform::Input>().wasPressed(platform::Key::Enter)) {
        ctx.resource<ScreenRequest>().next = Screen::Level;
    }
}

void winInput(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    if (input.wasPressed(platform::Key::Enter)) {
        ctx.resource<ScreenRequest>().next = Screen::Level;
    } else if (input.wasPressed(platform::Key::Escape)) {
        ctx.resource<ScreenRequest>().next = Screen::Menu;
    }
}

void levelInput(ecs::SystemContext& ctx) {
    if (ctx.resource<const platform::Input>().wasPressed(platform::Key::Escape)) {
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
      m_win(app.scenes().create("win")) {
    for (scene::Scene* screen : {&m_menu, &m_level, &m_win}) {
        screen->world().insertResource<ScreenRequest>();
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
    build(Screen::Menu);
    m_active = Screen::Menu;
    logCounts("at start");
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
        logCounts("after the switch");
    }
    // The screen left last frame no longer runs: its content can go.
    if (m_toClear) {
        clear(*m_toClear);
        m_toClear.reset();
        m_logCountdown = 2; // its assets unload at the end of this frame
    }

    const platform::Input& input = m_app.input();
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
    if (m_toClear == screen) { // left last frame and entered again at once
        clear(screen);
        m_toClear.reset();
    }
    build(screen);
    m_app.scenes().switchTo(sceneOf(screen));
    m_toClear = m_active;
    m_active = screen;
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
        spawnArt(target, m_tileset, kHero, {-1.0F, 0.0F});
        spawnArt(target, m_tileset, kCoin, {0.0F, 0.0F});
        spawnArt(target, m_tileset, kSlime, {1.0F, 0.0F});
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

void Flow::clear(Screen screen) {
    ecs::World& world = sceneOf(screen).world();
    std::vector<ecs::Entity> entities;
    entities.reserve(world.entityCount());
    world.forEachEntity([&](ecs::Entity entity) { entities.push_back(entity); });
    for (const ecs::Entity entity : entities) {
        world.destroy(entity);
    }
    // The resources the content added; the font's texture is released with the sheet.
    if (world.hasResource<GameState>()) {
        world.removeResource<GameState>();
    }
    if (world.hasResource<ScreenFont>()) {
        world.removeResource<ScreenFont>();
    }
}

void Flow::logCounts(const char* moment) {
    TESERRA_LOG_INFO(LogCategory::App, "Counts {}: {} assets; entities: menu {}, level {}, win {}", moment,
                     m_app.assets().size(), m_menu.world().entityCount(), m_level.world().entityCount(),
                     m_win.world().entityCount());
}

} // namespace demo
