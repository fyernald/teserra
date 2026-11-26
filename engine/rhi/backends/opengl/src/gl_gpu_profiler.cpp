// Tracy's OpenGL zones (tracy/TracyOpenGL.hpp) call GL through global function names. The glad loader of this
// backend has no globals (MX mode: one GladGLContext per device), so this file, the only one that includes
// Tracy's GL header, routes those names to the device's context. Built only with TESERRA_ENABLE_PROFILING.

#include "gl_gpu_profiler.hpp"

#include <array>
#include <cstring>
#include <optional>

namespace {

// The context of the device whose profiler runs; set while it lives (one device per thread).
const GladGLContext* teserraTracyGl = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

// The GL functions TracyOpenGL.hpp uses, on the device's context. The macros must have the GL names.
// NOLINTBEGIN(readability-identifier-naming)
#undef glGenQueries
#undef glGetInteger64v
#undef glGetQueryiv
#undef glGetQueryObjectiv
#undef glGetQueryObjectui64v
#undef glQueryCounter
#define glGenQueries teserraTracyGl->GenQueries
#define glGetInteger64v teserraTracyGl->GetInteger64v
#define glGetQueryiv teserraTracyGl->GetQueryiv
#define glGetQueryObjectiv teserraTracyGl->GetQueryObjectiv
#define glGetQueryObjectui64v teserraTracyGl->GetQueryObjectui64v
#define glQueryCounter teserraTracyGl->QueryCounter
// NOLINTEND(readability-identifier-naming)

#include <tracy/TracyOpenGL.hpp>

namespace teserra::rhi::opengl {

// Debug groups nest; deeper ones than this are not timed.
constexpr usize kMaxGpuZoneDepth = 16;

struct GpuProfiler::Zones {
    // False when another device of this thread owns Tracy's GPU context, or the driver lacks timer queries.
    bool active = false;
    std::array<std::optional<tracy::GpuCtxScope>, kMaxGpuZoneDepth> open{};
    usize depth = 0;
    usize skipped = 0; // groups opened past kMaxGpuZoneDepth
};

GpuProfiler::GpuProfiler(const GladGLContext& gl)
    : m_zones(std::make_unique<Zones>()) {
    const bool hasQueries = gl.GenQueries != nullptr && gl.GetInteger64v != nullptr &&
                            gl.GetQueryiv != nullptr && gl.GetQueryObjectiv != nullptr &&
                            gl.GetQueryObjectui64v != nullptr && gl.QueryCounter != nullptr;
    if (!hasQueries || tracy::GetGpuCtx().ptr != nullptr) {
        return;
    }
    m_zones->active = true;
    teserraTracyGl = &gl;
    TracyGpuContext;
    constexpr std::string_view kName = "OpenGL";
    TracyGpuContextName(kName.data(), static_cast<uint16_t>(kName.size()));
}

GpuProfiler::~GpuProfiler() {
    if (!m_zones->active) {
        return;
    }
    for (std::optional<tracy::GpuCtxScope>& zone : m_zones->open) {
        zone.reset();
    }
    // Tracy keeps one GPU context per thread and has no call to destroy it; a later device creates its own.
    tracy::GpuCtx* context = tracy::GetGpuCtx().ptr;
    if (context != nullptr) {
        context->~GpuCtx();
        tracy::tracy_free(context);
        tracy::GetGpuCtx().ptr = nullptr;
    }
    teserraTracyGl = nullptr;
}

void GpuProfiler::begin(std::string_view name) {
    Zones& zones = *m_zones;
    if (!zones.active) {
        return;
    }
    if (zones.depth == kMaxGpuZoneDepth) {
        ++zones.skipped;
        return;
    }
    zones.open[zones.depth].emplace(TracyLine, TracyFile, std::strlen(TracyFile), TracyFunction,
                                    std::strlen(TracyFunction), name.data(), name.size(), true);
    ++zones.depth;
}

void GpuProfiler::end() {
    Zones& zones = *m_zones;
    if (!zones.active) {
        return;
    }
    if (zones.skipped > 0) {
        --zones.skipped;
        return;
    }
    if (zones.depth > 0) {
        --zones.depth;
        zones.open[zones.depth].reset();
    }
}

void GpuProfiler::collect() {
    if (m_zones->active) {
        TracyGpuCollect;
    }
}

} // namespace teserra::rhi::opengl
