#pragma once

// The demo game's rules: components, the game state and the systems that move the hero and the slimes,
// collect coins, reset the hero on hazards, open the door and end the level at the goal.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/entity.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tileset.hpp>

namespace demo {

namespace ecs = teserra::ecs;
namespace scene = teserra::scene;
using teserra::f32;
using teserra::u32;
using teserra::UVec2;
using teserra::Vec2;

/// Art pixels per world unit; tiles are one unit.
inline constexpr f32 kPixelsPerUnit = 16.0F;
/// The view: 320x180 art pixels.
inline constexpr UVec2 kResolution{320, 180};
inline constexpr f32 kViewWidth = static_cast<f32>(kResolution.x) / kPixelsPerUnit;
inline constexpr f32 kViewHeight = static_cast<f32>(kResolution.y) / kPixelsPerUnit;

// Components.

/// The hero. `position` is exact; the Transform shows it rounded to whole art pixels.
struct Player {
    f32 speed = 5.0F;
    Vec2 position{0.0F};
};
/// What happens when the hero touches an entity.
enum class TouchKind : teserra::u8 {
    /// Collected.
    Coin,
    /// Sends the hero back to the start.
    Hazard,
    /// The door: open once every coin is collected; the hero entering it wins.
    Goal,
};
struct Touch {
    TouchKind kind = TouchKind::Coin;
};
/// Walks left and right, turning at walls.
struct Patrol {
    f32 speed = 2.0F;
    f32 direction = 1.0F;
    Vec2 position{0.0F};
};
/// The camera that follows the hero inside the level's bounds.
struct CameraFollow {};
/// One coin of the HUD, filled once `index` coins are collected.
struct HudSlot {
    u32 index = 0;
};

/// The state of a level: a resource of its scene.
struct GameState {
    const scene::Tileset* tileset = nullptr;
    ecs::Entity map{};
    ecs::Entity player{};
    Vec2 start{0.0F};
    /// The level's size in tiles.
    Vec2 size{0.0F};
    u32 coins = 0;
    u32 totalCoins = 0;
    u32 deaths = 0;
    f32 seconds = 0.0F;
    bool won = false;
};

/// Spawns the level into `level`: the tilemap, the hero, coins, spikes, slimes, the door, the camera and the
/// HUD, and inserts its GameState. `tileset` (whose sheet holds every Cell of art.hpp) must outlive the
/// scene.
void buildLevel(scene::Scene& level, const scene::Tileset& tileset);

/// Adds the game's systems to `level`.
void addGameplaySystems(scene::Scene& level);

} // namespace demo

TESERRA_COMPONENT(demo::Player, "Player");
TESERRA_COMPONENT(demo::Touch, "Touch");
TESERRA_COMPONENT(demo::Patrol, "Patrol");
TESERRA_COMPONENT(demo::CameraFollow, "CameraFollow");
TESERRA_COMPONENT(demo::HudSlot, "HudSlot");
