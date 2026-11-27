# Demo game

A small top-down game built only on Teserra's public API (iteration 14, #104). `teserra_check_public_includes()` fails the configuration if a source includes anything else.

Collect every coin in the cave, then reach the door. Spikes and slimes send you back to the start.

| Key | Action |
|---|---|
| Enter (gamepad: A or Start) | Start, or play again from the win screen |
| WASD / arrows (gamepad: left stick or d-pad) | Move |
| F1 | Show the colliders |
| Esc (gamepad: B) | Back to the menu; Esc quits from the menu |

## Feature list (frozen for #104)

- A 32x18 tile cave from an ASCII map (`gameplay.cpp`): a floor layer and a wall layer, solid through the tileset.
- The hero moves against the walls with `scene::moveAgainstTiles()`, on whole art pixels.
- 10 coins, collected on touch (`scene::CollisionStarted`), with a HUD row that fills up.
- Hazards: spikes, and slimes that patrol left and right and turn at walls. Touching either resets the hero to the start.
- The door opens once every coin is collected; entering it wins (logged with the time and the restart count).
- The camera follows the hero inside the level's bounds; the HUD is a child of the camera.
- 320x180 rendering, scaled by whole factors to the window (`AppConfig::sceneTarget`), with nearest filtering.

## Screens (#105)

The menu, the level and the win screen are three scenes of the App's `SceneManager` (`flow.cpp`). A screen's content is built when it is entered and cleared one frame after it is left, when it no longer runs. Each screen's systems request the next screen through a `ScreenRequest` resource, and `Flow::update()` acts on it between frames. The menu and the win screen use a 3x5 pixel font (`font.cpp`). The level does not, so switching to the level releases the font at the end of that frame, and the log shows the asset and entity counts after every switch. Playing again rebuilds the level with the same counts every time. The atlas is shared by all three screens.

## Art

All art is drawn in code (`art.cpp`) and is part of this repository under its license, so there is no third-party asset to attribute.
