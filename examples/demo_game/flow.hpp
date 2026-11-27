#pragma once

// The demo game's screens (#105): menu -> level -> win screen -> level again or the menu, each a scene of the
// App's SceneManager. A screen's content is built by its scene's enter hook and cleared by its exit hook, at
// the frame boundary of the switch (#253), so the assets only it used are released at the end of that frame.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tileset.hpp>

#include "gameplay.hpp"
#include "sound.hpp"

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

    /// The scene to run first: the menu, which its enter hook builds when the App runs it.
    [[nodiscard]] scene::Scene& start();
    /// Called once per frame, outside the scenes' systems: acts on the active screen's request. Escape on
    /// the menu quits; M mutes and unmutes the music on every screen.
    void update();

private:
    // Switches to `screen` at the next frame boundary.
    void enter(Screen screen);
    // The enter hook of `screen`'s scene.
    void build(Screen screen);
    [[nodiscard]] scene::Scene& sceneOf(Screen screen);
    // The font, loaded when the menu or the win screen needs it.
    [[nodiscard]] teserra::Result<teserra::renderer::SpriteSheet> font();
    void logCounts(const char* moment);

    teserra::engine::App& m_app;
    const scene::Tileset& m_tileset;
    scene::Scene& m_menu;
    scene::Scene& m_level;
    scene::Scene& m_win;
    // After the scenes: they hold copies of the clips' handles.
    Sounds m_sounds;
    Screen m_active = Screen::Menu;
    Results m_results;
    u32 m_plays = 0;
    // Frames until the counts are logged: after the end of the frame that cleared a screen.
    int m_logCountdown = -1;
};

} // namespace demo
