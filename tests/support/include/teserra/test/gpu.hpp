#pragma once

// GPU tests without a GPU (docs/guides/testing-strategy.md#gpu-integration-tests). A test that cannot get
// an OpenGL context skips with a message, so machines without a driver stay green. With the environment
// variable TESERRA_REQUIRE_GPU_TESTS=1 (CI's llvmpipe step) it fails instead, so a broken driver
// deployment cannot turn every GPU test into a silent skip.
//
//   if (!app.isInitialized()) {
//       TESERRA_SKIP_WITHOUT_GPU("No OpenGL 4.5 driver: " << std::format("{}", *app.initError()));
//   }

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace teserra::test {

/// Whether TESERRA_REQUIRE_GPU_TESTS is set to 1; read once.
[[nodiscard]] bool gpuTestsRequired() noexcept;

/// The backend GPU tests that can run on any backend use: TESERRA_GPU_BACKEND, "opengl" when unset. CI
/// runs its GPU step once per backend (a Vulkan backend joins as "vulkan").
[[nodiscard]] std::string_view gpuBackendName() noexcept;

} // namespace teserra::test

/// Skips the test with `message` (a Catch2 message expression), or fails it when GPU tests are required.
#define TESERRA_SKIP_WITHOUT_GPU(message)                                                                    \
    do {                                                                                                     \
        if (::teserra::test::gpuTestsRequired()) {                                                           \
            FAIL("GPU tests are required (TESERRA_REQUIRE_GPU_TESTS=1): " << message);                       \
        }                                                                                                    \
        SKIP(message);                                                                                       \
    } while (false)
