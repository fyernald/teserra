#pragma once

// Profiling instrumentation (ADR-0023). Engine code marks scopes, frames, allocations and threads with
// these macros as it is written. With TESERRA_ENABLE_PROFILING (the profile presets) they are Tracy's;
// otherwise they expand to nothing: arguments are not evaluated, no code is generated, and Tracy is not
// linked.
//
//   void Renderer::render() {
//       TESERRA_PROFILE_FUNCTION();
//       { TESERRA_PROFILE_SCOPE("Sort sprites"); ... }
//   }
//
//   TESERRA_PROFILE_SCOPE(name)          zone named by a string literal until the end of the enclosing scope
//   TESERRA_PROFILE_SCOPE_DYNAMIC(name)  zone named by a std::string_view known at run time (a system, a
//   pass) TESERRA_PROFILE_FUNCTION()           zone named after the enclosing function
//   TESERRA_PROFILE_FRAME_MARK()         end of a frame, once per main loop iteration
//   TESERRA_PROFILE_ALLOC(ptr, size)     memory allocation of `size` bytes at `ptr`
//   TESERRA_PROFILE_FREE(ptr)            release of the allocation at `ptr`
//   TESERRA_PROFILE_THREAD_NAME(name)    name of the calling thread in the profiler (a string literal)
//
// A scope macro declares variables, so it is a statement of its own; TESERRA_PROFILE_SCOPE and
// TESERRA_PROFILE_FUNCTION at most once per block.

#if defined(TESERRA_ENABLE_PROFILING) && TESERRA_ENABLE_PROFILING
#include <tracy/Tracy.hpp>

#include <cstring>
#include <string_view>

#define TESERRA_PROFILE_CONCAT_INNER(a, b) a##b
#define TESERRA_PROFILE_CONCAT(a, b) TESERRA_PROFILE_CONCAT_INNER(a, b)

#define TESERRA_PROFILE_SCOPE(name) ZoneScopedN(name)
// A transient zone: its source location carries the name, so the viewer and its exports group the zones
// of each system or pass under their own names, not under the function that ran them.
#define TESERRA_PROFILE_SCOPE_DYNAMIC(name)                                                                  \
    const std::string_view TESERRA_PROFILE_CONCAT(teserraZoneName, __LINE__) = (name);                       \
    const tracy::ScopedZone TESERRA_PROFILE_CONCAT(teserraZone, __LINE__)(                                   \
        TracyLine, TracyFile, std::strlen(TracyFile), TracyFunction, std::strlen(TracyFunction),             \
        TESERRA_PROFILE_CONCAT(teserraZoneName, __LINE__).data(),                                            \
        TESERRA_PROFILE_CONCAT(teserraZoneName, __LINE__).size(), -1, true)
#define TESERRA_PROFILE_FUNCTION() ZoneScoped
#define TESERRA_PROFILE_FRAME_MARK() FrameMark
#define TESERRA_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define TESERRA_PROFILE_FREE(ptr) TracyFree(ptr)
#define TESERRA_PROFILE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define TESERRA_PROFILE_SCOPE(name) static_cast<void>(0)
#define TESERRA_PROFILE_SCOPE_DYNAMIC(name) static_cast<void>(0)
#define TESERRA_PROFILE_FUNCTION() static_cast<void>(0)
#define TESERRA_PROFILE_FRAME_MARK() static_cast<void>(0)
#define TESERRA_PROFILE_ALLOC(ptr, size) static_cast<void>(0)
#define TESERRA_PROFILE_FREE(ptr) static_cast<void>(0)
#define TESERRA_PROFILE_THREAD_NAME(name) static_cast<void>(0)
#endif
