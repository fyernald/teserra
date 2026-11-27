# A minimal game on Teserra. Configure and build it with one command (see README.md):
#
#   cmake --workflow --preset debug
#
# Teserra comes from GitHub through FetchContent, pinned to a release tag. To build against a local clone
# instead, pass -DFETCHCONTENT_SOURCE_DIR_TESERRA=<path to the clone> when configuring.

cmake_minimum_required(VERSION 3.28)

project(MinimalGame VERSION 0.1.0 LANGUAGES CXX)

# No C++20 modules, as in Teserra: without this, CMake scans C++20 sources for them, which with Clang needs
# clang-scan-deps (not in Ubuntu's clang package).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(FetchContent)
FetchContent_Declare(teserra
    GIT_REPOSITORY https://github.com/fyernald/teserra.git
    GIT_TAG v0.2.0-rc.1
    GIT_SHALLOW TRUE
    # Teserra's headers are system headers for this project: your warning flags do not apply to them.
    SYSTEM
)
# Teserra keeps its tests and examples off when it is not the top-level project; nothing else to set.
FetchContent_MakeAvailable(teserra)

add_executable(minimal_game src/main.cpp)
target_compile_features(minimal_game PRIVATE cxx_std_20)
target_link_libraries(minimal_game PRIVATE Teserra::Engine)

# assets/ is copied next to the executable on every build, where the App finds it as assets://.
teserra_copy_assets(minimal_game)
