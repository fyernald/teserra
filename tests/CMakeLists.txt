# Cross-module tests. Unit tests of a single module live in engine/<module>/tests/.

# The build-system tests configure the whole engine, which the portable build cannot.
if(NOT TESERRA_PORTABLE_ONLY)
    add_subdirectory(cmake)
endif()

# With sanitizers, a deliberate use-after-free must be caught: the proof that the instrumentation is on (#113).
if(TESERRA_ENABLE_SANITIZERS)
    teserra_add_abort_test(sanitizers.use_after_free
        SOURCE sanitizers/use_after_free.cpp
        EXPECT "heap-use-after-free"
    )
    # LinearArena::reset() poisons the memory it takes back (#136).
    teserra_add_abort_test(sanitizers.arena_use_after_reset
        SOURCE sanitizers/arena_use_after_reset.cpp
        EXPECT "use-after-poison"
        DEPENDS core
    )
endif()

# The example of docs/architecture/public-api.md must compile (#119).
if(TARGET teserra_engine)
    add_subdirectory(public_api)
endif()

# The game template's source, built against this tree with the engine's warnings, and its smoke run (#120).
# CI also builds the template itself, with Teserra from FetchContent.
if(TARGET teserra_engine)
    add_subdirectory(template)
endif()

# The program of docs/guides/getting-started.md must compile and run (#121).
if(TARGET teserra_engine)
    add_subdirectory(getting_started)
endif()

# Snapshot tests render with the real backend and compare with golden images (#111).
if(TARGET teserra_engine AND TARGET teserra_rhi_opengl)
    add_subdirectory(snapshot)
endif()

# Unit tests of the source checks in tools/scripts (check_layers, check_gl_leaks); CI's checks job runs
# them on Linux, this runs them with the build's platform.
include(TeserraPython)
teserra_find_python()
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Teserra: tools.scripts test disabled (need Python 3)")
else()
    add_test(NAME tools.scripts
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${PROJECT_SOURCE_DIR}/tools/scripts/tests"
    )
    # The scripts' bytecode cache would otherwise land in the source tree.
    set_tests_properties(tools.scripts PROPERTIES
        LABELS "tools"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
    )
endif()
