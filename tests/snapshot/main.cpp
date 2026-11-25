// The snapshot test program: Catch2's own command line plus --update-goldens, which writes the rendered
// images as the new goldens (review them in the PR, docs/guides/testing-strategy.md#snapshot-tests).

#include "snapshot.hpp"
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    Catch::Session session;
    bool update = false;
    session.cli(session.cli() | Catch::Clara::Opt(update)["--update-goldens"](
                                    "write the rendered images as the new goldens"));
    if (const int status = session.applyCommandLine(argc, argv); status != 0) {
        return status;
    }
    teserra::snapshot::setUpdateGoldens(update);
    return session.run();
}
