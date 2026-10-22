#pragma once

// The OpenGL 4.5 backend of the RHI (docs/architecture/rhi.md#opengl-backend-mapping, ADR-0004). Engine code
// never sees OpenGL: it passes this backend to createDevice() and uses the Device interface.
//
//   auto device = rhi::createDevice({.backend = rhi::Backend::OpenGL}, &window,
//                                   std::array{rhi::opengl::backendFactory()});

#include <teserra/rhi/device.hpp>

namespace teserra::rhi::opengl {

/// The OpenGL backend, for createDevice(). Its devices need a window created with
/// platform::GraphicsApi::OpenGL, which must outlive the device; they fail with ErrorCode::Unsupported for
/// a headless device or a context below OpenGL 4.5.
[[nodiscard]] BackendFactory backendFactory() noexcept;

} // namespace teserra::rhi::opengl
