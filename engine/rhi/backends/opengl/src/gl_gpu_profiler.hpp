#pragma once

// GPU zones for Tracy on OpenGL timer queries (ADR-0023, #107): each debug group of a submitted command list
// (each render pass) becomes a zone on the GPU timeline. With TESERRA_ENABLE_PROFILING off, every call is an
// empty inline function and Tracy is not included.

#include <teserra/core/types.hpp>

#include <glad/gl.h>

#include <memory>
#include <string_view>

namespace teserra::rhi::opengl {

#if defined(TESERRA_ENABLE_PROFILING) && TESERRA_ENABLE_PROFILING

class GpuProfiler {
public:
    /// Creates Tracy's GPU context on `gl`, which must be current and outlive the profiler, on the device's
    /// thread. Tracy has one GPU context per thread: while another device's profiler holds it, or when the
    /// driver lacks timer queries, this profiler does nothing.
    explicit GpuProfiler(const GladGLContext& gl);
    ~GpuProfiler();

    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;
    GpuProfiler(GpuProfiler&&) = delete;
    GpuProfiler& operator=(GpuProfiler&&) = delete;

    /// Opens a zone named `name` at this point of the GL command stream.
    void begin(std::string_view name);
    /// Closes the innermost open zone.
    void end();
    /// Reads the finished queries; once per frame.
    void collect();

private:
    struct Zones;
    std::unique_ptr<Zones> m_zones;
};

#else

class GpuProfiler {
public:
    explicit GpuProfiler(const GladGLContext& /*gl*/) noexcept {}

    void begin(std::string_view /*name*/) noexcept {}
    void end() noexcept {}
    void collect() noexcept {}
};

#endif

} // namespace teserra::rhi::opengl
