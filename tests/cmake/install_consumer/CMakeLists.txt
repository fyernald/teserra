# Fixture project that uses an installed Teserra through find_package(), like a game built against an SDK
# (#143). Configured by tests/cmake/install/TestInstall.cmake with CMAKE_PREFIX_PATH set to the install prefix
# and to the packages Teserra was built with, and TESERRA_EXPECTED_VERSION to the version to ask for.

cmake_minimum_required(VERSION 3.28)
project(TeserraInstallConsumer LANGUAGES CXX)

# The version the test asks for: this build's major.minor, or another one that the package must refuse.
find_package(Teserra ${TESERRA_EXPECTED_VERSION} REQUIRED)

foreach(target IN ITEMS Teserra::Core Teserra::Audio Teserra::Scene Teserra::Engine)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Fixture: the installed package has no ${target}")
    endif()
endforeach()
if(NOT DEFINED Teserra_WITH_DEBUG_UI)
    message(FATAL_ERROR "Fixture: TeserraConfig.cmake does not say whether the debug UI is built in")
endif()

add_executable(install_consumer main.cpp)
target_link_libraries(install_consumer PRIVATE Teserra::Engine)
# On Windows, vcpkg builds GLFW, spdlog, fmt and yyjson as DLLs: copy them next to the executable, as a game does.
if(WIN32)
    add_custom_command(TARGET install_consumer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:install_consumer>
            $<TARGET_FILE_DIR:install_consumer>
        COMMAND_EXPAND_LISTS
    )
endif()
