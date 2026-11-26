add_executable(teserra_textured_quad main.cpp)
target_link_libraries(teserra_textured_quad PRIVATE Teserra::Engine Teserra::Assets)
teserra_set_warnings(teserra_textured_quad)

# The shaders are compiled at build time and embedded: <textured_quad/shaders.hpp> declares
# teserra::examples::shaders::kQuadVert and kQuadFrag.
teserra_add_shaders(teserra_textured_quad EMBED
    NAMESPACE teserra::examples::shaders
    HEADER textured_quad/shaders.hpp
    SOURCES
        shaders/quad.vert
        shaders/quad.frag
)

# The example reads its image from assets/ next to the executable. The copy runs on every build and only
# writes when the file changed, so an edited image reaches the build without relinking the example.
teserra_copy_assets(teserra_textured_quad)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(textured_quad TARGET teserra_textured_quad FRAMES 120)
endif()
