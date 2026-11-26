#include "art.hpp"

#include <array>
#include <cmath>

namespace demo {

using teserra::f32;
using teserra::u32;
using teserra::u8;
using teserra::usize;

namespace {

using Texel = std::array<u8, 4>;

constexpr Texel kClear{0, 0, 0, 0};
constexpr Texel kOutline{24, 20, 37, 255};

// Distance of a texel's center from the cell's center.
f32 distanceFromCenter(u32 x, u32 y, f32 centerY = 7.5F) {
    const f32 dx = static_cast<f32>(x) - 7.5F;
    const f32 dy = static_cast<f32>(y) - centerY;
    return std::sqrt((dx * dx) + (dy * dy));
}

Texel floor(u32 x, u32 y) {
    const bool speck = ((x * 5) + (y * 11)) % 13 == 0;
    return speck ? Texel{70, 62, 88, 255} : Texel{52, 46, 68, 255};
}

Texel wall(u32 x, u32 y) {
    const bool mortar = y % 8 == 7 || (x + ((y / 8) * 8)) % 16 == 15;
    const bool light = y % 8 == 0;
    return mortar ? Texel{38, 32, 52, 255} : light ? Texel{150, 134, 170, 255} : Texel{116, 102, 138, 255};
}

Texel spikes(u32 x, u32 y) {
    // Three teeth on the floor.
    const u32 tooth = x % 5;
    const u32 height = tooth <= 2 ? tooth * 4 : (4 - tooth) * 4;
    if (x < 15 && 15 - y <= height + 2 && y >= 4) {
        return y % 3 == 0 ? Texel{230, 230, 240, 255} : Texel{180, 184, 200, 255};
    }
    return floor(x, y);
}

Texel door(u32 x, u32 y, bool open) {
    const bool frame = x <= 1 || x >= 14 || y <= 1;
    if (frame) {
        return Texel{92, 64, 40, 255};
    }
    if (open) {
        return y > 3 && x > 3 && x < 12 ? Texel{255, 236, 140, 255} : Texel{40, 30, 20, 255};
    }
    const bool plank = x % 4 == 1;
    const bool lock = x >= 7 && x <= 8 && y >= 8 && y <= 10;
    return lock ? Texel{240, 200, 60, 255} : plank ? Texel{110, 72, 40, 255} : Texel{150, 100, 56, 255};
}

Texel hero(u32 x, u32 y) {
    const f32 d = distanceFromCenter(x, y, 8.5F);
    if (y == 7 && (x == 5 || x == 10)) {
        return kOutline; // eyes
    }
    if (d < 5.5F) {
        return y < 6 ? Texel{120, 200, 255, 255} : Texel{76, 160, 236, 255};
    }
    return d < 6.5F ? kOutline : kClear;
}

Texel coin(u32 x, u32 y) {
    const f32 d = distanceFromCenter(x, y);
    if (d < 2.0F) {
        return Texel{255, 248, 190, 255};
    }
    if (d < 4.5F) {
        return Texel{248, 196, 48, 255};
    }
    return d < 5.5F ? Texel{170, 110, 20, 255} : kClear;
}

Texel slime(u32 x, u32 y) {
    // A dome on the floor.
    const f32 dx = static_cast<f32>(x) - 7.5F;
    const f32 dy = static_cast<f32>(y) - 15.0F;
    const f32 d = std::sqrt((dx * dx) + (dy * dy * 1.6F));
    if (y == 10 && (x == 5 || x == 10)) {
        return kOutline;
    }
    if (d < 7.0F) {
        return y < 9 ? Texel{150, 236, 110, 255} : Texel{96, 196, 76, 255};
    }
    return d < 8.0F && y < 16 ? kOutline : kClear;
}

Texel coinSlot(u32 x, u32 y) {
    const f32 d = distanceFromCenter(x, y);
    return d >= 4.0F && d < 5.5F ? Texel{110, 100, 130, 255} : kClear;
}

} // namespace

std::vector<std::byte> drawAtlas() {
    std::vector<std::byte> texels(usize{kAtlasWidth} * kCellSize * 4);
    for (u32 cell = 0; cell < CellCount; ++cell) {
        for (u32 y = 0; y < kCellSize; ++y) {
            for (u32 x = 0; x < kCellSize; ++x) {
                Texel color{};
                switch (static_cast<Cell>(cell)) {
                    case Floor:
                        color = floor(x, y);
                        break;
                    case Wall:
                        color = wall(x, y);
                        break;
                    case Spikes:
                        color = spikes(x, y);
                        break;
                    case DoorClosed:
                        color = door(x, y, false);
                        break;
                    case DoorOpen:
                        color = door(x, y, true);
                        break;
                    case Hero:
                        color = hero(x, y);
                        break;
                    case Coin:
                        color = coin(x, y);
                        break;
                    case Slime:
                        color = slime(x, y);
                        break;
                    case CoinSlot:
                        color = coinSlot(x, y);
                        break;
                    case CellCount:
                        break;
                }
                const usize at = ((usize{y} * kAtlasWidth) + (usize{cell} * kCellSize) + x) * 4;
                for (usize channel = 0; channel < 4; ++channel) {
                    texels[at + channel] = std::byte{color[channel]};
                }
            }
        }
    }
    return texels;
}

} // namespace demo
