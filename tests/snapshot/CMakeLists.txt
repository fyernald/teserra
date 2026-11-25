# Snapshot tests (docs/guides/testing-strategy.md#snapshot-tests): scenes rendered by the real backend and
# compared with tests/golden/*.png. They need a GPU driver: they skip without one, and CI runs them on Mesa
# llvmpipe with TESERRA_REQUIRE_GPU_TESTS=1. Update the goldens with `teserra_snapshot_tests --update-goldens`.
teserra_add_test(snapshot
    NO_MAIN
    SOURCES
        comparator_test.cpp
        image_compare.cpp
        image_compare.hpp
        main.cpp
        snapshot.cpp
        snapshot.hpp
        snapshot_test.cpp
    DEPENDS engine
)
target_compile_definitions(teserra_snapshot_tests PRIVATE "TESERRA_GOLDEN_DIR=\"${PROJECT_SOURCE_DIR}/tests/golden\"")
# stb_image_write writes the PNGs (stb is already a dependency of the assets module, ADR-0010).
target_include_directories(teserra_snapshot_tests SYSTEM PRIVATE "${TESERRA_STB_INCLUDE_DIR}")

# The triangle snapshot's shaders: <snapshot_tests/shaders.hpp> declares teserra::test::shaders::kVertexColorVert
# and kVertexColorFrag.
teserra_add_shaders(teserra_snapshot_tests EMBED
    NAMESPACE teserra::test::shaders
    HEADER snapshot_tests/shaders.hpp
    SOURCES
        shaders/vertex_color.vert
        shaders/vertex_color.frag
)
