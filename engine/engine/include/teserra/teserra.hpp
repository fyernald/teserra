#pragma once

// Everything a game usually needs, in one include (docs/architecture/public-api.md): the App, scenes, the
// ECS, components for sprites, cameras, tilemaps and collision, input, assets, debug draw and math. Link
// Teserra::Engine. Lower-level headers (<teserra/rhi/...>, <teserra/renderer/...>) stay available for
// advanced use. Names keep their namespaces: engine::App, scene::Transform, ecs::SystemContext.

#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/events.hpp>
#include <teserra/ecs/schedule.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/ecs/type_key.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/debug_draw.hpp>
#include <teserra/renderer/material.hpp>
#include <teserra/renderer/sprite_sheet.hpp>
#include <teserra/scene/collision.hpp>
#include <teserra/scene/hierarchy.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tile_queries.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>
#include <teserra/scene/time.hpp>
#include <teserra/scene/transform.hpp>
