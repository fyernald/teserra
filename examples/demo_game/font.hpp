#pragma once

// A 3x5 pixel font drawn in code, and text made of one sprite per glyph: enough for the menu and the win
// screen until the engine renders text (#137).

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/scene.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace demo {

/// A glyph's cell in the font texture: 3x5 texels and a 1-texel gap.
inline constexpr teserra::u32 kGlyphWidth = 4;
inline constexpr teserra::u32 kGlyphHeight = 6;

/// The characters of the font, in texture order. Others draw as a space.
inline constexpr std::string_view kFontCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:/!-";

/// The font texture's RGBA8 texels: white glyphs on transparent texels, fontWidth() x kGlyphHeight.
[[nodiscard]] std::vector<std::byte> drawFont();
[[nodiscard]] constexpr teserra::u32 fontWidth() noexcept {
    return static_cast<teserra::u32>(kFontCharacters.size()) * kGlyphWidth;
}

/// Spawns `text` into `target` as glyph sprites of `font` (a grid of kGlyphWidth x kGlyphHeight cells in
/// kFontCharacters order), centered on `center`, `scale` art pixels per font texel.
void spawnText(teserra::scene::Scene& target, const teserra::renderer::SpriteSheet& font,
               std::string_view text, teserra::Vec2 center, teserra::f32 scale, teserra::Color tint,
               teserra::i16 layer = 50);

} // namespace demo
