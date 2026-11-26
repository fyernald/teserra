add_executable(teserra_tilemap main.cpp)
target_link_libraries(teserra_tilemap PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_tilemap)

# The example reads its tileset as assets://tiles.png, which the App mounts at assets/ next to the
# executable. The copy runs on every build and only writes when the file changed.
teserra_copy_assets(teserra_tilemap)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(tilemap TARGET teserra_tilemap FRAMES 120)
endif()
