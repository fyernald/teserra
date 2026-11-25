#include "gameplay.hpp"

#include <teserra/core/log.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/scene/collision.hpp>
#include <teserra/scene/hierarchy.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/tile_queries.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>

#include "art.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace demo {

using teserra::Aabb;
using teserra::IVec2;
using teserra::LogCategory;
using teserra::Vec3;
namespace platform = teserra::platform;
namespace math = teserra::math;

namespace {

// The level, top row first: # wall, . floor, S start, c coin, x spikes, e slime, G the door.
constexpr std::array<std::string_view, 18> kLevel{
    "################################", "#S.....#.........c.....#......c#",
    "#......#.......####....#.......#", "#..c...#..x....#..#....###.##..#",
    "#......#..x....#c.#............#", "###.####..x....#..####..e......#",
    "#..............#.......#####.###", "#..c....e......####....#.......#",
    "#.......................c.#....#", "#####.#######.######.######....#",
    "#.......#...........#......#...#", "#..c....#....xxx....#..c...#...#",
    "#.......#...........#......#.c.#", "#...e...####.####.###......#...#",
    "#..............................#", "#.c..............e.........##..#",
    "#..........#######.........#G..#", "################################",
};

// Tile ids of the tileset: cell + 1.
constexpr scene::TileId kFloorTile = kFloor + 1;
constexpr scene::TileId kWallTile = kWall + 1;

// Sprite layers, bottom to top.
constexpr teserra::i16 kPropLayer = 1;
constexpr teserra::i16 kCoinLayer = 5;
constexpr teserra::i16 kSlimeLayer = 8;
constexpr teserra::i16 kHeroLayer = 10;
constexpr teserra::i16 kHudLayer = 100;

// A position rounded to whole art pixels, so pixel art never shimmers.
Vec2 snapToPixels(Vec2 position) {
    return Vec2{std::round(position.x * kPixelsPerUnit), std::round(position.y * kPixelsPerUnit)} /
           kPixelsPerUnit;
}

// The center of tile (column, row) of kLevel; rows count from the top, world +Y is up.
Vec2 tileCenter(int column, int row) {
    return {static_cast<f32>(column) + 0.5F,
            static_cast<f32>(static_cast<int>(kLevel.size()) - 1 - row) + 0.5F};
}

scene::SpriteRenderer sprite(const scene::Tileset& tileset, Cell cell, teserra::i16 layer) {
    scene::SpriteRenderer made = scene::spriteFromSheet(tileset.sheet(), cell);
    made.layer = layer;
    return made;
}

ecs::Entity spawnSprite(scene::Scene& level, const scene::Tileset& tileset, Cell cell, Vec2 position,
                        teserra::i16 layer) {
    const ecs::Entity entity = level.spawn();
    level.add<scene::Transform>(entity, scene::Transform{.position = {position, 0.0F}});
    level.add<scene::SpriteRenderer>(entity, sprite(tileset, cell, layer));
    return entity;
}

// Moves a box by `delta` against the level's walls.
template <typename Context>
scene::MoveResult moveInLevel(Context& ctx, const GameState& state, Aabb box, Vec2 delta) {
    const auto* map = ctx.template tryGet<const scene::Tilemap>(state.map);
    const auto* global = ctx.template tryGet<const scene::GlobalTransform>(state.map);
    const scene::TilemapLayer* walls = map != nullptr ? map->findLayer("walls") : nullptr;
    if (walls == nullptr || global == nullptr) {
        return {.moved = delta};
    }
    const scene::TilemapQuery query(*map, global->matrix);
    return scene::moveAgainstTiles(
        query, *walls, [&](scene::TileId tile) { return state.tileset->isSolid(tile); }, box, delta);
}

// Systems.

void movePlayer(ecs::SystemContext& ctx) {
    GameState& state = ctx.resource<GameState>();
    if (state.won) {
        return;
    }
    const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
    state.seconds += dt;
    const auto& input = ctx.resource<const platform::Input>();
    Vec2 direction =
        input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W) +
        input.axis2D(platform::Key::Left, platform::Key::Right, platform::Key::Down, platform::Key::Up);
    if (math::length(direction) > 1.0F) {
        direction = math::normalize(direction);
    }
    for (auto [entity, transform, player, collider] :
         ctx.view<scene::Transform, Player, const scene::Collider>()) {
        const Aabb box = scene::colliderBounds(collider, Vec3{player.position, 0.0F});
        player.position += moveInLevel(ctx, state, box, direction * player.speed * dt).moved;
        transform.setPosition2D(snapToPixels(player.position));
    }
}

void patrol(ecs::SystemContext& ctx) {
    const GameState& state = ctx.resource<const GameState>();
    const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
    for (auto [entity, transform, walker, collider] :
         ctx.view<scene::Transform, Patrol, const scene::Collider>()) {
        const Aabb box = scene::colliderBounds(collider, Vec3{walker.position, 0.0F});
        const scene::MoveResult moved =
            moveInLevel(ctx, state, box, {walker.direction * walker.speed * dt, 0.0F});
        walker.position += moved.moved;
        if (moved.hitX) {
            walker.direction = -walker.direction;
        }
        transform.setPosition2D(snapToPixels(walker.position));
    }
}

// What the hero touched this frame: coins, hazards and the door.
void react(ecs::SystemContext& ctx) {
    GameState& state = ctx.resource<GameState>();
    bool collected = false;
    ctx.readEvents<scene::CollisionStarted>([&](const scene::CollisionStarted& touch) {
        if (state.won || (touch.a != state.player && touch.b != state.player)) {
            return;
        }
        const ecs::Entity other = touch.a == state.player ? touch.b : touch.a;
        const Touch* touched = ctx.tryGet<const Touch>(other);
        if (touched == nullptr) {
            return;
        }
        if (touched->kind == TouchKind::Coin) {
            ctx.commands().destroy(other);
            ++state.coins;
            collected = true;
        } else if (touched->kind == TouchKind::Hazard) {
            ++state.deaths;
            if (Player* player = ctx.tryGet<Player>(state.player)) {
                player->position = state.start;
            }
            if (scene::Transform* transform = ctx.tryGet<scene::Transform>(state.player)) {
                transform->setPosition2D(state.start);
            }
        } else if (touched->kind == TouchKind::Goal && state.coins == state.totalCoins) {
            state.won = true;
            TESERRA_LOG_INFO(LogCategory::App, "You win! {} coins in {:.1f} s, {} restarts", state.coins,
                             state.seconds, state.deaths);
        }
    });
    if (!collected) {
        return;
    }
    // The HUD fills up, and the last coin opens the door.
    for (auto [entity, renderer, slot] : ctx.view<scene::SpriteRenderer, const HudSlot>()) {
        renderer.uv =
            scene::spriteFromSheet(state.tileset->sheet(), slot.index < state.coins ? kCoin : kCoinSlot).uv;
    }
    if (state.coins == state.totalCoins) {
        for (auto [entity, renderer, touched] : ctx.view<scene::SpriteRenderer, const Touch>()) {
            if (touched.kind == TouchKind::Goal) {
                renderer.uv = scene::spriteFromSheet(state.tileset->sheet(), kDoorOpen).uv;
            }
        }
        TESERRA_LOG_INFO(LogCategory::App, "Every coin collected: the door is open");
    }
}

// Keeps the camera on the hero, inside the level, on whole art pixels.
void followPlayer(ecs::SystemContext& ctx) {
    const GameState& state = ctx.resource<const GameState>();
    const Player* player = ctx.tryGet<const Player>(state.player);
    if (player == nullptr) {
        return;
    }
    const auto clampAxis = [](f32 value, f32 view, f32 size) {
        return size <= view ? size * 0.5F : std::clamp(value, view * 0.5F, size - (view * 0.5F));
    };
    const Vec2 target{clampAxis(player->position.x, kViewWidth, state.size.x),
                      clampAxis(player->position.y, kViewHeight, state.size.y)};
    for (auto [entity, transform, follow] : ctx.view<scene::Transform, const CameraFollow>()) {
        transform.setPosition2D(snapToPixels(target));
    }
}

} // namespace

void buildLevel(scene::Scene& level, const scene::Tileset& tileset) {
    GameState state{.tileset = &tileset,
                    .size = {static_cast<f32>(kLevel[0].size()), static_cast<f32>(kLevel.size())}};

    // The tilemap: floor everywhere, walls on their own layer for collision.
    scene::Tilemap map;
    scene::TilemapLayer& floor = map.addLayer("floor");
    for (int row = 0; row < static_cast<int>(kLevel.size()); ++row) {
        for (int column = 0; column < static_cast<int>(kLevel[row].size()); ++column) {
            floor.setTile({column, static_cast<int>(kLevel.size()) - 1 - row}, kFloorTile);
        }
    }
    scene::TilemapLayer& walls = map.addLayer("walls");
    for (int row = 0; row < static_cast<int>(kLevel.size()); ++row) {
        for (int column = 0; column < static_cast<int>(kLevel[row].size()); ++column) {
            if (kLevel[row][column] == '#') {
                walls.setTile({column, static_cast<int>(kLevel.size()) - 1 - row}, kWallTile);
            }
        }
    }
    state.map = level.spawn();
    level.add<scene::Transform>(state.map);
    level.add<scene::Tilemap>(state.map, std::move(map));
    level.add<scene::TilemapRenderer>(state.map, scene::TilemapRenderer{.tileset = &tileset});

    // Everything else stands on a tile.
    for (int row = 0; row < static_cast<int>(kLevel.size()); ++row) {
        for (int column = 0; column < static_cast<int>(kLevel[row].size()); ++column) {
            const Vec2 at = tileCenter(column, row);
            switch (kLevel[row][column]) {
                case 'S':
                    state.start = at;
                    break;
                case 'c': {
                    const ecs::Entity coin = spawnSprite(level, tileset, kCoin, at, kCoinLayer);
                    level.add<scene::Collider>(coin, scene::Collider{.size = Vec2{0.5F}});
                    level.add<Touch>(coin, Touch{.kind = TouchKind::Coin});
                    ++state.totalCoins;
                    break;
                }
                case 'x': {
                    const ecs::Entity spikes = spawnSprite(level, tileset, kSpikes, at, kPropLayer);
                    level.add<scene::Collider>(spikes, scene::Collider{.size = Vec2{0.75F}});
                    level.add<Touch>(spikes, Touch{.kind = TouchKind::Hazard});
                    break;
                }
                case 'e': {
                    const ecs::Entity slime = spawnSprite(level, tileset, kSlime, at, kSlimeLayer);
                    level.add<scene::Collider>(
                        slime, scene::Collider{.size = {0.8F, 0.5F}, .offset = {0.0F, -0.2F}});
                    level.add<Touch>(slime, Touch{.kind = TouchKind::Hazard});
                    level.add<Patrol>(slime, Patrol{.position = at});
                    break;
                }
                case 'G': {
                    const ecs::Entity door = spawnSprite(level, tileset, kDoorClosed, at, kPropLayer);
                    level.add<scene::Collider>(door, scene::Collider{.size = Vec2{0.5F}});
                    level.add<Touch>(door, Touch{.kind = TouchKind::Goal});
                    break;
                }
                default:
                    break;
            }
        }
    }

    state.player = spawnSprite(level, tileset, kHero, state.start, kHeroLayer);
    level.add<scene::Collider>(state.player, scene::Collider{.size = Vec2{0.75F}});
    level.add<Player>(state.player, Player{.position = state.start});

    // The camera shows 320x180 art pixels; the HUD's coin slots are its children, so they stay on screen.
    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera, scene::Transform{.position = {state.start, 0.0F}});
    level.add<scene::Camera>(camera,
                             scene::Camera{.projection = scene::Orthographic{.height = kViewHeight},
                                           .clearColor = {.r = 0.09F, .g = 0.08F, .b = 0.13F, .a = 1.0F}});
    level.add<CameraFollow>(camera);
    for (u32 index = 0; index < state.totalCoins; ++index) {
        const Vec2 at{(-kViewWidth * 0.5F) + 0.75F + static_cast<f32>(index), (kViewHeight * 0.5F) - 0.75F};
        const ecs::Entity slot = spawnSprite(level, tileset, kCoinSlot, at, kHudLayer);
        level.add<HudSlot>(slot, HudSlot{.index = index});
        scene::setParent(level.world(), slot, camera);
    }

    TESERRA_LOG_INFO(LogCategory::App, "Collect the {} coins, then reach the door. Avoid spikes and slimes.",
                     state.totalCoins);
    level.world().insertResource<GameState>(state);
}

void addGameplaySystems(scene::Scene& level) {
    level.systems()
        .add(ecs::Phase::Update, "move_player", movePlayer)
        .writes<Player>()
        .writes<scene::Transform>()
        .reads<scene::Collider>()
        .reads<scene::Tilemap>()
        .reads<scene::GlobalTransform>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>()
        .writesResource<GameState>();
    level.systems()
        .add(ecs::Phase::Update, "patrol", patrol)
        .writes<Patrol>()
        .writes<scene::Transform>()
        .reads<scene::Collider>()
        .reads<scene::Tilemap>()
        .reads<scene::GlobalTransform>()
        .readsResource<scene::Time>()
        .readsResource<GameState>()
        .after("move_player");
    level.systems()
        .add(ecs::Phase::PostUpdate, "follow_player", followPlayer)
        .reads<Player>()
        .reads<CameraFollow>()
        .writes<scene::Transform>()
        .readsResource<GameState>()
        .before(scene::kTransformSystemName);
    level.systems()
        .add(ecs::Phase::PostUpdate, "react", react)
        .reads<Touch>()
        .reads<HudSlot>()
        .writes<Player>()
        .writes<scene::Transform>()
        .writes<scene::SpriteRenderer>()
        .writesResource<GameState>()
        .readsEvents<scene::CollisionStarted>()
        .after(scene::kCollisionSystemName);
}

} // namespace demo
