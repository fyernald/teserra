// The mapping of KHR_debug messages to log levels and names.

#include "gl_debug.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace teserra;
using namespace teserra::rhi::opengl;

TEST_CASE("debugMessageLevel: severity and type map to log levels", "[rhi_opengl][debug]") {
    constexpr std::array kSeverities{GL_DEBUG_SEVERITY_HIGH, GL_DEBUG_SEVERITY_MEDIUM, GL_DEBUG_SEVERITY_LOW,
                                     GL_DEBUG_SEVERITY_NOTIFICATION};

    // Errors are errors whatever the driver says their severity is.
    for (const GLenum severity : kSeverities) {
        CHECK(debugMessageLevel(severity, GL_DEBUG_TYPE_ERROR) == LogLevel::Error);
    }
    // Deprecated and undefined behavior are at least warnings.
    for (const GLenum type : {GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR}) {
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_HIGH, type) == LogLevel::Error);
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_MEDIUM, type) == LogLevel::Warn);
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_LOW, type) == LogLevel::Warn);
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_NOTIFICATION, type) == LogLevel::Warn);
    }
    // Everything else follows the severity.
    for (const GLenum type :
         {GL_DEBUG_TYPE_PERFORMANCE, GL_DEBUG_TYPE_PORTABILITY, GL_DEBUG_TYPE_OTHER, GL_DEBUG_TYPE_MARKER}) {
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_HIGH, type) == LogLevel::Error);
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_MEDIUM, type) == LogLevel::Warn);
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_LOW, type) == LogLevel::Info);
        CHECK(debugMessageLevel(GL_DEBUG_SEVERITY_NOTIFICATION, type) == LogLevel::Debug);
    }
}

TEST_CASE("debugSourceName and debugTypeName", "[rhi_opengl][debug]") {
    CHECK(debugSourceName(GL_DEBUG_SOURCE_API) == "API");
    CHECK(debugSourceName(GL_DEBUG_SOURCE_WINDOW_SYSTEM) == "Window system");
    CHECK(debugSourceName(GL_DEBUG_SOURCE_SHADER_COMPILER) == "Shader compiler");
    CHECK(debugSourceName(GL_DEBUG_SOURCE_THIRD_PARTY) == "Third party");
    CHECK(debugSourceName(GL_DEBUG_SOURCE_APPLICATION) == "Application");
    CHECK(debugSourceName(GL_DEBUG_SOURCE_OTHER) == "Other");

    CHECK(debugTypeName(GL_DEBUG_TYPE_ERROR) == "Error");
    CHECK(debugTypeName(GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR) == "Deprecated behavior");
    CHECK(debugTypeName(GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR) == "Undefined behavior");
    CHECK(debugTypeName(GL_DEBUG_TYPE_PORTABILITY) == "Portability");
    CHECK(debugTypeName(GL_DEBUG_TYPE_PERFORMANCE) == "Performance");
    CHECK(debugTypeName(GL_DEBUG_TYPE_MARKER) == "Marker");
    CHECK(debugTypeName(GL_DEBUG_TYPE_PUSH_GROUP) == "Push group");
    CHECK(debugTypeName(GL_DEBUG_TYPE_POP_GROUP) == "Pop group");
    CHECK(debugTypeName(GL_DEBUG_TYPE_OTHER) == "Other");
}
