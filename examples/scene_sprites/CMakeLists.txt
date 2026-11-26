add_executable(teserra_scene_sprites main.cpp)
target_link_libraries(teserra_scene_sprites PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_scene_sprites)

# The example reads its sprite sheet as assets://sprites.png, which the App mounts at assets/ next to the
# executable. The copy runs on every build and only writes when the file changed.
teserra_copy_assets(teserra_scene_sprites)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(scene_sprites TARGET teserra_scene_sprites FRAMES 120)
endif()
