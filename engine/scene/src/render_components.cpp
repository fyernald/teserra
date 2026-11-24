#include <teserra/scene/render_components.hpp>

namespace teserra::scene {

SpriteRenderer spriteFromSheet(const renderer::SpriteSheet& sheet, usize region) {
    const assets::Asset<renderer::Texture>& texture = sheet.texture();
    return {
        .texture = texture.isReady() ? texture.get().handle() : rhi::TextureHandle{},
        .uv = sheet.uv(region),
        .size = sheet.worldSize(region),
    };
}

std::optional<SpriteRenderer> spriteFromSheet(const renderer::SpriteSheet& sheet, std::string_view name) {
    const std::optional<usize> region = sheet.find(name);
    return region ? std::optional<SpriteRenderer>(spriteFromSheet(sheet, *region)) : std::nullopt;
}

} // namespace teserra::scene
