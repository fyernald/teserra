# Cross-module tests. Unit tests of a single module live in engine/<module>/tests/.

# The build-system tests configure the whole engine, which the portable build cannot.
if(NOT TESERRA_PORTABLE_ONLY)
    add_subdirectory(cmake)
endif()

# Snapshot tests render with the real backend and compare with golden images (#111).
if(TARGET teserra_engine AND TARGET teserra_rhi_opengl)
    add_subdirectory(snapshot)
endif()

# Unit tests of the source checks in tools/scripts (check_layers, check_gl_leaks); CI's checks job runs
# them on Linux, this runs them with the build's platform.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME tools.scripts
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${PROJECT_SOURCE_DIR}/tools/scripts/tests"
    )
    # The scripts' bytecode cache would otherwise land in the source tree.
    set_tests_properties(tools.scripts PROPERTIES
        LABELS "tools"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
    )
endif()
