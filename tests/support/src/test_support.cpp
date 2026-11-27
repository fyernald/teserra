// Linked into every test executable from teserra_add_test() (an OBJECT library, so the listener below
// is always registered).

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>
#endif

namespace teserra::test {

AssertionFailure::AssertionFailure(const AssertInfo& info)
    : m_kind(info.kind),
      m_expression(info.expression),
      m_message(info.message),
      m_file(info.location.file_name()),
      m_line(static_cast<u32>(info.location.line())),
      m_description(describeAssert(info)) {
}

void throwingAssertHandler(const AssertInfo& info) {
    throw AssertionFailure(info);
}

namespace {

// A failed assert() or iterator check of the Debug CRT (tl::expected::error() on a value, a vector index out
// of range) opens a message box by default, and an unattended test run then waits for its timeout. Tests
// report them on stderr instead, and abort() without a dialog, so Catch2 and CTest see a crash.
void reportCrtFailuresOnStderr() {
#if defined(_WIN32)
#if defined(_DEBUG) // the report functions are empty macros in a release CRT
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

class TestRunListener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*testRunInfo*/) override {
        // Tests observe logging through callback sinks; console output would only add noise.
        log::LogConfig config;
        config.console = false;
        static_cast<void>(log::configure(config));

        setAssertHandler(&throwingAssertHandler);
        reportCrtFailuresOnStderr();
    }
};

} // namespace

} // namespace teserra::test

CATCH_REGISTER_LISTENER(teserra::test::TestRunListener)
