// Program for the abort test core.assert.fatal_aborts: with the default handler, TESERRA_FATAL logs at
// Critical, flushes and ends the process. If it returned, main() would end normally with exit code 0
// and the test would fail.

#include <teserra/core/assert.hpp>

int main() {
    TESERRA_FATAL("GPU device lost: {}", "driver reset");
}
