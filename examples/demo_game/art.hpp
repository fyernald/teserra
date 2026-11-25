#pragma once

// The demo game's art: one 16x16-cell atlas drawn in code, so the game ships no image files.

#include <teserra/core/types.hpp>

#include <cstddef>
#include <vector>

namespace demo {

/// The cells of the atlas, left to right. The first two are also the tileset's tiles: TileId = cell + 1.
enum Cell : teserra::u32 {
    kFloor,
    kWall,
    kSpikes,
    kDoorClosed,
    kDoorOpen,
    kHero,
    kCoin,
    kSlime,
    kCoinSlot, // an empty HUD slot
    kCellCount,
};

inline constexpr teserra::u32 kCellSize = 16;
inline constexpr teserra::u32 kAtlasWidth = kCellSize * kCellCount;

/// The atlas's RGBA8 texels, kAtlasWidth x kCellSize, top row first.
[[nodiscard]] std::vector<std::byte> drawAtlas();

} // namespace demo
