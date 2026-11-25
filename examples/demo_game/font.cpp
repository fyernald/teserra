#include "font.hpp"

#include <teserra/scene/render_components.hpp>
#include <teserra/scene/transform.hpp>

#include "gameplay.hpp"

#include <array>
#include <cmath>

namespace demo {

using teserra::Color;
using teserra::f32;
using teserra::u32;
using teserra::usize;
using teserra::Vec2;

namespace {

// Five rows of three texels per glyph, top row first, in kFontCharacters order.
constexpr std::array<std::array<std::string_view, 5>, kFontCharacters.size()> kGlyphs{{
    {"###", "#.#", "###", "#.#", "#.#"}, // A
    {"##.", "#.#", "##.", "#.#", "##."}, // B
    {"###", "#..", "#..", "#..", "###"}, // C
    {"##.", "#.#", "#.#", "#.#", "##."}, // D
    {"###", "#..", "##.", "#..", "###"}, // E
    {"###", "#..", "##.", "#..", "#.."}, // F
    {"###", "#..", "#.#", "#.#", "###"}, // G
    {"#.#", "#.#", "###", "#.#", "#.#"}, // H
    {"###", ".#.", ".#.", ".#.", "###"}, // I
    {"..#", "..#", "..#", "#.#", "###"}, // J
    {"#.#", "#.#", "##.", "#.#", "#.#"}, // K
    {"#..", "#..", "#..", "#..", "###"}, // L
    {"#.#", "###", "###", "#.#", "#.#"}, // M
    {"##.", "#.#", "#.#", "#.#", "#.#"}, // N
    {"###", "#.#", "#.#", "#.#", "###"}, // O
    {"###", "#.#", "###", "#..", "#.."}, // P
    {"###", "#.#", "#.#", "###", "..#"}, // Q
    {"##.", "#.#", "##.", "#.#", "#.#"}, // R
    {"###", "#..", "###", "..#", "###"}, // S
    {"###", ".#.", ".#.", ".#.", ".#."}, // T
    {"#.#", "#.#", "#.#", "#.#", "###"}, // U
    {"#.#", "#.#", "#.#", "#.#", ".#."}, // V
    {"#.#", "#.#", "###", "###", "#.#"}, // W
    {"#.#", "#.#", ".#.", "#.#", "#.#"}, // X
    {"#.#", "#.#", ".#.", ".#.", ".#."}, // Y
    {"###", "..#", ".#.", "#..", "###"}, // Z
    {"###", "#.#", "#.#", "#.#", "###"}, // 0
    {".#.", "##.", ".#.", ".#.", "###"}, // 1
    {"###", "..#", "###", "#..", "###"}, // 2
    {"###", "..#", "###", "..#", "###"}, // 3
    {"#.#", "#.#", "###", "..#", "..#"}, // 4
    {"###", "#..", "###", "..#", "###"}, // 5
    {"###", "#..", "###", "#.#", "###"}, // 6
    {"###", "..#", "..#", "..#", "..#"}, // 7
    {"###", "#.#", "###", "#.#", "###"}, // 8
    {"###", "#.#", "###", "..#", "###"}, // 9
    {"...", "...", "...", "...", ".#."}, // .
    {"...", ".#.", "...", ".#.", "..."}, // :
    {"..#", "..#", ".#.", "#..", "#.."}, // /
    {".#.", ".#.", ".#.", "...", ".#."}, // !
    {"...", "...", "###", "...", "..."}, // -
}};

} // namespace

std::vector<std::byte> drawFont() {
    const u32 width = fontWidth();
    std::vector<std::byte> texels(usize{width} * kGlyphHeight * 4, std::byte{0});
    for (usize glyph = 0; glyph < kGlyphs.size(); ++glyph) {
        for (usize row = 0; row < 5; ++row) {
            for (usize column = 0; column < 3; ++column) {
                if (kGlyphs[glyph][row][column] != '#') {
                    continue;
                }
                const usize at = ((row * width) + (glyph * kGlyphWidth) + column) * 4;
                for (usize channel = 0; channel < 4; ++channel) {
                    texels[at + channel] = std::byte{255};
                }
            }
        }
    }
    return texels;
}

void spawnText(teserra::scene::Scene& target, const teserra::renderer::SpriteSheet& font,
               std::string_view text, Vec2 center, f32 scale, Color tint, teserra::i16 layer) {
    namespace scene = teserra::scene;
    // Glyphs are kGlyphWidth texels apart; the last one's gap does not count.
    const f32 texel = scale / kPixelsPerUnit;
    const Vec2 glyphSize{static_cast<f32>(kGlyphWidth) * texel, static_cast<f32>(kGlyphHeight) * texel};
    const f32 width = (static_cast<f32>(text.size()) * glyphSize.x) - texel;
    // The left edge on a whole art pixel, so the glyphs' texels stay crisp.
    const f32 left = std::round((center.x - (width * 0.5F)) * kPixelsPerUnit) / kPixelsPerUnit;
    const f32 bottom = std::round((center.y - (glyphSize.y * 0.5F)) * kPixelsPerUnit) / kPixelsPerUnit;
    for (usize i = 0; i < text.size(); ++i) {
        const usize glyph = kFontCharacters.find(text[i]);
        if (glyph == std::string_view::npos) {
            continue; // a space, or a character the font does not have
        }
        scene::SpriteRenderer sprite = scene::spriteFromSheet(font, glyph);
        sprite.size = glyphSize;
        sprite.pivot = {0.0F, 1.0F}; // the bottom-left corner
        sprite.tint = tint;
        sprite.layer = layer;
        const teserra::ecs::Entity entity = target.spawn();
        target.add<scene::Transform>(
            entity, scene::Transform{.position = {left + (static_cast<f32>(i) * glyphSize.x), bottom, 0.0F}});
        target.add<scene::SpriteRenderer>(entity, sprite);
    }
}

} // namespace demo
