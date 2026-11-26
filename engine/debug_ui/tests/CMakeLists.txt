teserra_add_test(debug_ui
    SOURCES
        debug_ui_input_test.cpp
        imgui_pass_test.cpp
    # The null backend runs the pass without a GPU (docs/guides/testing-strategy.md#rhi-and-renderer-tests).
    DEPENDS debug_ui rhi_null
)

# Every public header compiles on its own.
teserra_add_header_check(debug_ui)
