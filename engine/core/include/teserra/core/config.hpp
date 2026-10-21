#pragma once

// Platform, compiler and build-type detection. This header only inspects predefined macros and
// includes nothing, so it is safe to include from anywhere.
//
// Exactly one TESERRA_PLATFORM_* and one TESERRA_COMPILER_* macro is defined (to 1). Using the
// platform macros outside engine/platform/ and engine/core/ needs a review comment (ADR-0015).

// Platform

#if defined(_WIN32)
#define TESERRA_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define TESERRA_PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
#define TESERRA_PLATFORM_MACOS 1
#else
#error "Teserra: unsupported platform"
#endif

// Compiler. clang-cl defines both __clang__ and _MSC_VER, and Clang defines __GNUC__, so the order of
// these checks matters.

#if defined(__clang__) && defined(_MSC_VER)
#define TESERRA_COMPILER_CLANG_CL 1
#elif defined(__clang__)
#define TESERRA_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define TESERRA_COMPILER_GCC 1
#elif defined(_MSC_VER)
#define TESERRA_COMPILER_MSVC 1
#else
#error "Teserra: unsupported compiler"
#endif

// Language level (ADR-0002). MSVC reports the real value of __cplusplus only with /Zc:__cplusplus,
// which consumers of the engine may not pass, so prefer _MSVC_LANG there.

#if defined(_MSVC_LANG)
#define TESERRA_CPLUSPLUS _MSVC_LANG
#else
#define TESERRA_CPLUSPLUS __cplusplus
#endif

#if TESERRA_CPLUSPLUS < 202002L
#error "Teserra requires C++20"
#endif

// Build type. TESERRA_DEBUG_BUILD is always defined: 1 when NDEBUG is not defined (Debug
// configuration), 0 otherwise. It can be used both in #if and as a constant expression.

#if defined(NDEBUG)
#define TESERRA_DEBUG_BUILD 0
#else
#define TESERRA_DEBUG_BUILD 1
#endif
