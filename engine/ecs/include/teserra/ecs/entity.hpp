#pragma once

// Entities (docs/architecture/ecs.md#entities): generational handles (ADR-0020) that a World hands out.

#include <teserra/core/handle.hpp>

namespace teserra::ecs {

/// An entity of a World: a 32-bit index and a 32-bit generation. The default value is the null entity. An
/// entity stays valid until the World destroys it; after that it is stale and never alive again, even when
/// its index is reused. Entities never move between worlds.
using Entity = Handle<struct EntityTag>;

} // namespace teserra::ecs
