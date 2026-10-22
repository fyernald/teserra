#include <teserra/rhi_opengl/opengl_backend.hpp>

#include "gl_device.hpp"

namespace teserra::rhi::opengl {

BackendFactory backendFactory() noexcept {
    return {.backend = Backend::OpenGL, .create = &GlDevice::create};
}

} // namespace teserra::rhi::opengl
