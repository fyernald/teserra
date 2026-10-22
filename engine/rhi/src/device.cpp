#include <teserra/rhi/device.hpp>

#include <teserra/core/assert.hpp>

#include <algorithm>

namespace teserra::rhi {

std::string_view backendName(Backend backend) noexcept {
    switch (backend) {
        case Backend::OpenGL:
            return "OpenGL";
        case Backend::Vulkan:
            return "Vulkan";
        case Backend::Null:
            return "Null";
    }
    return "Unknown";
}

Result<std::unique_ptr<Device>> createDevice(const DeviceDesc& desc, platform::Window* window,
                                             std::span<const BackendFactory> backends) {
    const auto matches = [&](const BackendFactory& factory) { return factory.backend == desc.backend; };
    TESERRA_ASSERT(std::ranges::count_if(backends, matches) <= 1, "Backend {} is offered more than once",
                   backendName(desc.backend));

    const auto factory = std::ranges::find_if(backends, matches);
    if (factory == backends.end()) {
        return fail(ErrorCode::Unsupported, Module::Rhi, "The {} backend is not part of this build",
                    backendName(desc.backend));
    }
    TESERRA_ASSERT(factory->create != nullptr, "The {} backend factory has no create function",
                   backendName(desc.backend));
    return factory->create(desc, window);
}

} // namespace teserra::rhi
