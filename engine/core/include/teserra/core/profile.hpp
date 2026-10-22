#pragma once

// Profiling instrumentation (ADR-0023). Engine code marks scopes, frames, allocations and threads with
// these macros as it is written. Until the Tracy integration exists they expand to nothing: arguments
// are not evaluated and no code is generated.
//
//   void Renderer::render() {
//       TESERRA_PROFILE_FUNCTION();
//       { TESERRA_PROFILE_SCOPE("Sort sprites"); ... }
//   }
//
//   TESERRA_PROFILE_SCOPE(name)          named zone until the end of the enclosing scope
//   TESERRA_PROFILE_FUNCTION()           zone named after the enclosing function
//   TESERRA_PROFILE_FRAME_MARK()         end of a frame, once per main loop iteration
//   TESERRA_PROFILE_ALLOC(ptr, size)     memory allocation of `size` bytes at `ptr`
//   TESERRA_PROFILE_FREE(ptr)            release of the allocation at `ptr`
//   TESERRA_PROFILE_THREAD_NAME(name)    name of the calling thread in the profiler

#if defined(TESERRA_ENABLE_PROFILING) && TESERRA_ENABLE_PROFILING
// TODO(#107): Tracy-backed implementations behind the TESERRA_ENABLE_PROFILING option.
#error "TESERRA_ENABLE_PROFILING is not supported yet: the Tracy integration arrives with issue #107"
#else
#define TESERRA_PROFILE_SCOPE(name) static_cast<void>(0)
#define TESERRA_PROFILE_FUNCTION() static_cast<void>(0)
#define TESERRA_PROFILE_FRAME_MARK() static_cast<void>(0)
#define TESERRA_PROFILE_ALLOC(ptr, size) static_cast<void>(0)
#define TESERRA_PROFILE_FREE(ptr) static_cast<void>(0)
#define TESERRA_PROFILE_THREAD_NAME(name) static_cast<void>(0)
#endif
