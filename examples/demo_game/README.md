# Demo game

A small top-down game built only on Teserra's public API (iteration 14, #104). `teserra_check_public_includes()` fails the configuration if a source includes anything else.

Collect every coin in the cave, then reach the door. Spikes and slimes send you back to the start.

| Key | Action |
|---|---|
| WASD / arrows | Move |
| F1 | Show the colliders |
| Esc | Quit |

## Feature list (frozen for #104)

- A 32x18 tile cave from an ASCII map (`gameplay.cpp`): a floor layer and a wall layer, solid through the tileset.
- The hero moves against the walls with `scene::moveAgainstTiles()`, on whole art pixels.
- 10 coins, collected on touch (`scene::CollisionStarted`), with a HUD row that fills up.
- Hazards: spikes, and slimes that patrol left and right and turn at walls. Touching either resets the hero to the start.
- The door opens once every coin is collected; entering it wins (logged with the time and the restart count).
- The camera follows the hero inside the level's bounds; the HUD is a child of the camera.
- 320x180 rendering, scaled by whole factors to the window (`AppConfig::sceneTarget`), with nearest filtering.

Scene switching (menu, level, win screen) comes with #105.

## Art

All art is drawn in code (`art.cpp`) and is part of this repository under its license, so there is no third-party asset to attribute.
