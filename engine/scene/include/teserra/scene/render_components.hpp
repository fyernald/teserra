#pragma once

// The components that make a scene visible: cameras and sprites (docs/architecture/renderer.md#inputs-render-
// data-not-entities). The extraction system turns them into the renderer's FramePacket.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/renderer/camera.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/scene/tileset.hpp>

#include <optional>
#include <string_view>

namespace teserra::scene {

/// A view of the scene. An entity with a Camera and a Transform renders the scene from its GlobalTransform
/// every frame; the camera looks down its local -Z axis (ADR-0024). It is the renderer's camera description
/// used as a component: projection, viewport, order and clear color.
///
///     const ecs::Entity camera = level.spawn();
///     level.add<Transform>(camera);
///     level.add<Camera>(camera, Camera{.projection = Orthographic{.height = 18.0F}});
using Camera = renderer::Camera;
using renderer::Orthographic;
using renderer::Perspective;
using renderer::SpriteBlend;
using renderer::UvRect;

/// Draws a textured quad at an entity's GlobalTransform. The fields are those of renderer::SpriteDrawItem
/// without the world matrix, which comes from the entity.
///
///     level.add<SpriteRenderer>(hero, SpriteRenderer{.texture = heroTexture, .layer = 10});
struct SpriteRenderer {
    /// The texture: a renderer::Texture asset's handle() or one created on the App's device. The game keeps
    /// the asset (or the sprite sheet) alive while sprites show it.
    rhi::TextureHandle texture{};
    /// The part of the texture shown, top-left origin.
    UvRect uv{};
    /// World units, before the transform's scale.
    Vec2 size{1.0F};
    /// The point of the image at the entity's position, normalized like UVs: {0, 0} is the image's top-left
    /// corner, {1, 1} its bottom-right one.
    Vec2 pivot{0.5F};
    /// Multiplies the texture, in linear values.
    Color tint{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F};
    SpriteBlend blend = SpriteBlend::Alpha;
    /// Higher layers draw over lower ones.
    i16 layer = 0;
    /// Orders sprites within a layer: higher values draw later, on top. Top-down games use -y.
    f32 sortDepth = 0.0F;
    /// Hidden sprites are not extracted.
    bool visible = true;
};

/// A SpriteRenderer that shows region `region` of `sheet`: the sheet's texture, the region's UVs and its size
/// in world units. The other fields keep their defaults. The sheet must outlive the sprites that show it.
///
///     level.add<SpriteRenderer>(coin, spriteFromSheet(sheet, *sheet.find("coin")));
[[nodiscard]] SpriteRenderer spriteFromSheet(const renderer::SpriteSheet& sheet, usize region);

/// Like spriteFromSheet(sheet, region) for the region named `name`; std::nullopt if the sheet has none.
[[nodiscard]] std::optional<SpriteRenderer> spriteFromSheet(const renderer::SpriteSheet& sheet,
                                                            std::string_view name);

/// Draws the Tilemap of its entity, placed by the entity's GlobalTransform, with the tiles of `tileset`.
/// Tilemap layer i draws at sprite layer `layer + i * layerSpacing`, before the sprites of that layer; a
/// spacing above 1 leaves layers for sprites between tile layers.
///
///     level.add<Tilemap>(map, std::move(tiles));
///     level.add<TilemapRenderer>(map, TilemapRenderer{.tileset = &tileset, .layerSpacing = 10});
struct TilemapRenderer {
    /// The tiles' art. The game keeps the tileset alive while the tilemap shows it.
    const Tileset* tileset = nullptr;
    /// Multiplies every tile, in linear values.
    Color tint{.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 1.0F};
    SpriteBlend blend = SpriteBlend::Alpha;
    /// The sprite layer of the first tilemap layer.
    i16 layer = 0;
    /// Sprite layers between consecutive tilemap layers.
    i16 layerSpacing = 1;
    /// Hidden tilemaps are not extracted.
    bool visible = true;
};

} // namespace teserra::scene

TESERRA_COMPONENT(teserra::renderer::Camera, "Camera");
TESERRA_COMPONENT(teserra::scene::SpriteRenderer, "SpriteRenderer");
TESERRA_COMPONENT(teserra::scene::TilemapRenderer, "TilemapRenderer");
