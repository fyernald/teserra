#pragma once

// The backend of the App-level GPU tests: TESERRA_GPU_BACKEND (test::gpuBackendName()), OpenGL by default, so
// that CI's GPU step can run the same tests on each backend.

#include <teserra/rhi/device.hpp>
#include <teserra/test/gpu.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace teserra::test {

inline rhi::Backend gpuTestBackend() {
    const std::string_view name = gpuBackendName();
    if (name != "opengl") {
        FAIL("Unknown TESERRA_GPU_BACKEND '" << name << "' (known: opengl)");
    }
    return rhi::Backend::OpenGL;
}

} // namespace teserra::test
