add_executable(teserra_rhi_triangle main.cpp)
target_link_libraries(teserra_rhi_triangle PRIVATE Teserra::Engine)
teserra_set_warnings(teserra_rhi_triangle)

# The shaders are compiled at build time and embedded: <rhi_triangle/shaders.hpp> declares
# teserra::examples::shaders::kTriangleVert and kTriangleFrag.
teserra_add_shaders(teserra_rhi_triangle EMBED
    NAMESPACE teserra::examples::shaders
    HEADER rhi_triangle/shaders.hpp
    SOURCES
        shaders/triangle.vert
        shaders/triangle.frag
)

# Smoke runs (docs/guides/testing-strategy.md#example-smoke-runs): 120 frames, also with the debug UI open.
if(TESERRA_BUILD_TESTS)
    teserra_add_smoke_run(rhi_triangle TARGET teserra_rhi_triangle FRAMES 120)
endif()
