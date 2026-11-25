#pragma once

// The demo game's screens (#105): menu -> level -> win screen -> level again or the menu, each a scene of the
// App's SceneManager. A screen's content is built when it is entered and cleared one frame after it is left,
// once it no longer runs, so the assets only it used are released at the end of that frame.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tileset.hpp>

#include "gameplay.hpp"

#include <optional>

namespace demo {

enum class Screen : teserra::u8 {
    Menu,
    Level,
    Win,
};

/// A screen's request to switch, set by its systems: a resource of every screen's scene.
struct ScreenRequest {
    std::optional<Screen> next;
};

/// What the win screen shows.
struct Results {
    u32 coins = 0;
    u32 totalCoins = 0;
    u32 restarts = 0;
    f32 seconds = 0.0F;
};

/// The font of the menu and the win screen: a texture asset and its glyphs. A resource of those screens'
/// scenes while they are built.
struct ScreenFont {
    teserra::renderer::SpriteSheet glyphs;
};

class Flow {
public:
    /// Creates the three scenes and adds their systems. `tileset` must outlive the App's scenes.
    Flow(teserra::engine::App& app, const scene::Tileset& tileset);

    /// The scene to run first: the menu, built.
    [[nodiscard]] scene::Scene& start();
    /// Called once per frame, outside the scenes' systems: clears the screen left last frame, and acts on
    /// the active screen's request. Escape on the menu quits.
    void update();

private:
    void enter(Screen screen);
    void build(Screen screen);
    // Destroys every entity of `screen`'s scene and the resources its content added.
    void clear(Screen screen);
    [[nodiscard]] scene::Scene& sceneOf(Screen screen);
    // The font, loaded when the menu or the win screen needs it.
    [[nodiscard]] teserra::Result<teserra::renderer::SpriteSheet> font();
    void logCounts(const char* moment);

    teserra::engine::App& m_app;
    const scene::Tileset& m_tileset;
    scene::Scene& m_menu;
    scene::Scene& m_level;
    scene::Scene& m_win;
    Screen m_active = Screen::Menu;
    std::optional<Screen> m_toClear;
    Results m_results;
    u32 m_plays = 0;
    // Frames until the counts are logged: after the end of the frame that cleared a screen.
    int m_logCountdown = -1;
};

} // namespace demo
