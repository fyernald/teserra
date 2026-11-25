#include <teserra/test/gpu.hpp>

#include <teserra/core/types.hpp>

#include <cstdlib>
#include <string>

namespace teserra::test {

namespace {

// The value of environment variable `name`, empty if it is not set.
std::string environment(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    usize size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string copy(value);
    std::free(value); // NOLINT(cppcoreguidelines-no-malloc): _dupenv_s allocates with malloc
    return copy;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read once, before tests run
    return value != nullptr ? std::string(value) : std::string();
#endif
}

} // namespace

bool gpuTestsRequired() noexcept {
    static const bool kRequired = environment("TESERRA_REQUIRE_GPU_TESTS") == "1";
    return kRequired;
}

std::string_view gpuBackendName() noexcept {
    static const std::string kBackend = [] {
        std::string name = environment("TESERRA_GPU_BACKEND");
        return name.empty() ? std::string("opengl") : name;
    }();
    return kBackend;
}

} // namespace teserra::test
