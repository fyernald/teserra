#pragma once

// The demo game's art: one 16x16-cell atlas drawn in code, so the game ships no image files.

#include <teserra/core/types.hpp>

#include <cstddef>
#include <vector>

namespace demo {

/// The cells of the atlas, left to right. The first two are also the tileset's tiles: TileId = cell + 1.
enum Cell : teserra::u8 {
    Floor,
    Wall,
    Spikes,
    DoorClosed,
    DoorOpen,
    Hero,
    Coin,
    Slime,
    CoinSlot, // an empty HUD slot
    CellCount,
};

inline constexpr teserra::u32 kCellSize = 16;
inline constexpr teserra::u32 kAtlasWidth = kCellSize * CellCount;

/// The atlas's RGBA8 texels, kAtlasWidth x kCellSize, top row first.
[[nodiscard]] std::vector<std::byte> drawAtlas();

} // namespace demo
