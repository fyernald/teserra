add_executable(teserra_sound main.cpp)
target_link_libraries(teserra_sound PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_sound)
# An example of the public API only.
teserra_check_public_includes(teserra_sound)

# The example reads assets://blip.wav and assets://tune.ogg (streamed, by its sidecar), which the App mounts at
# assets/ next to the executable.
teserra_copy_assets(teserra_sound)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(sound TARGET teserra_sound FRAMES 120)
endif()
