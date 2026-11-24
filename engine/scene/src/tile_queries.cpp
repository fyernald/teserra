#include <teserra/scene/tile_queries.hpp>

#include <limits>

namespace teserra::scene {

namespace {

// Tile coordinates far outside any map are clamped, so float-to-int conversion stays defined.
constexpr f32 kCoordLimit = 1.0e9F;

i32 toCoord(f32 value) noexcept {
    return static_cast<i32>(std::clamp(value, -kCoordLimit, kCoordLimit));
}

Vec3 transformPoint(const Mat4& matrix, Vec3 point) noexcept {
    return Vec3{matrix * Vec4{point, 1.0F}};
}

} // namespace

IVec2 localToTile(const Tilemap& map, Vec2 local) noexcept {
    return {toCoord(std::floor(local.x / map.tileSize.x)), toCoord(std::floor(local.y / map.tileSize.y))};
}

TilemapQuery::TilemapQuery(const Tilemap& map, const Mat4& world) noexcept
    : m_map(&map), m_world(world), m_toLocal(math::inverse(world)) {
}

IVec2 TilemapQuery::worldToTile(Vec3 worldPos) const noexcept {
    const Vec3 local = transformPoint(m_toLocal, worldPos);
    return localToTile(*m_map, {local.x, local.y});
}

Vec3 TilemapQuery::tileCenter(IVec2 coord) const noexcept {
    const Vec2 center = (Vec2{coord} + Vec2{0.5F}) * m_map->tileSize;
    return transformPoint(m_world, {center.x, center.y, 0.0F});
}

Aabb TilemapQuery::tileBounds(IVec2 coord) const noexcept {
    const Vec2 min = Vec2{coord} * m_map->tileSize;
    const Vec2 max = min + m_map->tileSize;
    const Vec3 first = transformPoint(m_world, {min.x, min.y, 0.0F});
    Aabb bounds{.min = first, .max = first};
    for (const Vec2 corner : {Vec2{max.x, min.y}, Vec2{min.x, max.y}, max}) {
        const Vec3 point = transformPoint(m_world, {corner.x, corner.y, 0.0F});
        bounds = math::merge(bounds, Aabb{.min = point, .max = point});
    }
    return bounds;
}

std::array<IVec2, 2> TilemapQuery::tileRange(const Aabb& worldBox) const noexcept {
    // The box's local bounds in tile units, from its eight corners.
    Vec2 min{std::numeric_limits<f32>::max()};
    Vec2 max{std::numeric_limits<f32>::lowest()};
    for (u32 corner = 0; corner < 8; ++corner) {
        const Vec3 world{(corner & 1U) != 0 ? worldBox.max.x : worldBox.min.x,
                         (corner & 2U) != 0 ? worldBox.max.y : worldBox.min.y,
                         (corner & 4U) != 0 ? worldBox.max.z : worldBox.min.z};
        const Vec3 local = transformPoint(m_toLocal, world);
        const Vec2 tiles = Vec2{local.x, local.y} / m_map->tileSize;
        min = {std::min(min.x, tiles.x), std::min(min.y, tiles.y)};
        max = {std::max(max.x, tiles.x), std::max(max.y, tiles.y)};
    }
    // Tile x is reached when the box overlaps (x, x + 1): from floor(min) to ceil(max) - 1, and at least the
    // tile of min when the box has no extent.
    const IVec2 first{toCoord(std::floor(min.x)), toCoord(std::floor(min.y))};
    const IVec2 last{std::max(first.x, toCoord(std::ceil(max.x)) - 1),
                     std::max(first.y, toCoord(std::ceil(max.y)) - 1)};
    return {first, last};
}

} // namespace teserra::scene
