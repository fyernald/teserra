teserra_add_test(renderer
    SOURCES
        renderer_test.cpp
        upload_ring_test.cpp
    # The null backend runs the renderer without a GPU (docs/guides/testing-strategy.md#rhi-and-renderer-tests).
    DEPENDS renderer rhi_null
)

# Every public header compiles on its own.
teserra_add_header_check(renderer)
