teserra_add_module(debug_ui LAYER 6
    # The in-game debug tools (ADR-0013): Dear ImGui with its own renderer backend on the RHI, drawn as an
    # overlay pass of the renderer. Only built with TESERRA_WITH_DEBUG_UI. The engine module owns it.
    DEPENDS core platform rhi assets renderer ecs scene
    PUBLIC_HEADERS
        include/teserra/debug_ui/debug_ui.hpp
        include/teserra/debug_ui/entity_inspector.hpp
        include/teserra/debug_ui/imgui_pass.hpp
        include/teserra/debug_ui/log_console.hpp
    SOURCES
        src/debug_ui.cpp
        src/entity_inspector.cpp
        src/imgui_input.cpp
        src/imgui_pass.cpp
        src/log_console.cpp
)

# PUBLIC: games write their own debug windows with ImGui:: calls (ADR-0013); no module below this one sees
# ImGui.
find_package(imgui CONFIG REQUIRED)
teserra_link_third_party(debug_ui PUBLIC imgui::imgui)

# The ImGui pass's shaders, compiled at build time and embedded: <teserra_debug_ui/shaders.hpp> declares
# teserra::debugui::shaders::kImguiVert and kImguiFrag.
teserra_add_shaders(teserra_debug_ui EMBED
    NAMESPACE teserra::debugui::shaders
    HEADER teserra_debug_ui/shaders.hpp
    SOURCES
        shaders/imgui.vert
        shaders/imgui.frag
)

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
