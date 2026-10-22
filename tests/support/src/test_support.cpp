// Linked into every test executable from teserra_add_test() (an OBJECT library, so the listener below
// is always registered).

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

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

class TestRunListener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*testRunInfo*/) override {
        // Tests observe logging through callback sinks; console output would only add noise.
        log::LogConfig config;
        config.console = false;
        static_cast<void>(log::configure(config));

        setAssertHandler(&throwingAssertHandler);
    }
};

} // namespace

} // namespace teserra::test

CATCH_REGISTER_LISTENER(teserra::test::TestRunListener)
